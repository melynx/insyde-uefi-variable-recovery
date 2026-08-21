// SPDX-License-Identifier: GPL-2.0-only
/*
 * Work around an Insyde GetNextVariableName() first-record bug.
 *
 * The firmware loses its main variable store when enumeration starts with an
 * empty name after ExitBootServices(). An exact GetVariable() call still
 * works, and GetNextVariableName() continues through the main store when it is
 * seeded with a known runtime variable. This module validates a configurable
 * seed and creates missing in-memory efivarfs entries. It never calls
 * SetVariable().
 */

#include <linux/efi.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/magic.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ucs2_string.h>
#include <linux/uuid.h>

#define RECOVERY_NAME_BYTES 512
#define NORMAL_COUNT_LIMIT 32
#define RECOVERY_COUNT_LIMIT 4096
#define EFIVARFS_DEFAULT_PATH "/sys/firmware/efi/efivars"

static bool expose;
module_param(expose, bool, 0400);
MODULE_PARM_DESC(expose,
	"Create missing in-memory efivarfs entries (default: false, dry run)");

static char *efivarfs_path = EFIVARFS_DEFAULT_PATH;
module_param(efivarfs_path, charp, 0400);
MODULE_PARM_DESC(efivarfs_path, "Path to the mounted efivarfs filesystem");

static unsigned int normal_count_limit = NORMAL_COUNT_LIMIT;
module_param(normal_count_limit, uint, 0400);
MODULE_PARM_DESC(normal_count_limit,
	"Largest normal enumeration count treated as collapsed");

static char *seed_name = "PlatformLang";
module_param(seed_name, charp, 0400);
MODULE_PARM_DESC(seed_name,
	"ASCII UEFI variable name used to enter the main-store walk");

static char *seed_guid = "8be4df61-93ca-11d2-aa0d-00e098032b8c";
module_param(seed_guid, charp, 0400);
MODULE_PARM_DESC(seed_guid, "GUID of seed_name in canonical text form");

static efi_guid_t parsed_seed_guid;
static efi_char16_t parsed_seed_name[EFI_VAR_NAME_LEN / sizeof(efi_char16_t)];
static unsigned long parsed_seed_name_size;

struct recovered_variable {
	struct list_head link;
	efi_guid_t guid;
	unsigned long name_size;
	unsigned long data_size;
	u32 attributes;
	char *path;
	efi_char16_t name[];
};

static bool variable_matches(const efi_char16_t *name,
			     const efi_guid_t *guid,
			     const efi_char16_t *wanted_name,
			     const efi_guid_t *wanted_guid)
{
	return guid_equal(guid, wanted_guid) &&
	       !ucs2_strncmp(name, wanted_name, EFI_VAR_NAME_LEN / sizeof(*name));
}

static unsigned long bounded_name_size(const efi_char16_t *name,
				       unsigned long supplied_size)
{
	unsigned long offset;

	if (supplied_size < sizeof(*name) ||
	    supplied_size > RECOVERY_NAME_BYTES ||
	    supplied_size % sizeof(*name) || !name[0])
		return 0;

	for (offset = 0; offset + sizeof(*name) <= supplied_size;
	     offset += sizeof(*name)) {
		if (!name[offset / sizeof(*name)])
			return offset + sizeof(*name);
	}

	return 0;
}

static int inspect_normal_enumeration(unsigned int *count,
				      bool *seed_seen,
				      efi_status_t *final_status)
{
	efi_char16_t *name;
	efi_guid_t guid = NULL_GUID;
	efi_status_t status = EFI_SUCCESS;
	unsigned long name_size;
	int error;

	*count = 0;
	*seed_seen = false;
	name = kzalloc(RECOVERY_NAME_BYTES, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	error = efivar_lock();
	if (error)
		goto out;

	while (*count < RECOVERY_COUNT_LIMIT) {
		name_size = RECOVERY_NAME_BYTES;
		status = efivar_get_next_variable(&name_size, name, &guid);
		if (status != EFI_SUCCESS)
			break;
		if (!bounded_name_size(name, name_size)) {
			status = EFI_INVALID_PARAMETER;
			break;
		}
		(*count)++;
		if (variable_matches(name, &guid, parsed_seed_name,
				     &parsed_seed_guid))
			*seed_seen = true;
	}

	efivar_unlock();
	*final_status = status;
	if (*count == RECOVERY_COUNT_LIMIT)
		error = -ELOOP;
out:
	kfree(name);
	return error;
}

static bool list_contains(struct list_head *variables,
			  const efi_char16_t *name, const efi_guid_t *guid)
{
	struct recovered_variable *variable;

	list_for_each_entry(variable, variables, link) {
		if (variable_matches(variable->name, &variable->guid, name, guid))
			return true;
	}

	return false;
}

/* The caller holds efivar_lock(). */
static int add_exact_variable(struct list_head *variables,
			      const efi_char16_t *name,
			      unsigned long name_size,
			      const efi_guid_t *guid,
			      bool *added)
{
	struct recovered_variable *variable;
	efi_status_t status;
	unsigned long data_size = 0;
	u32 attributes = 0;

	*added = false;
	if (list_contains(variables, name, guid))
		return 0;

	status = efivar_get_variable((efi_char16_t *)name, (efi_guid_t *)guid,
				     &attributes, &data_size, NULL);
	if (status != EFI_BUFFER_TOO_SMALL && status != EFI_SUCCESS) {
		pr_warn("insyde_efivarfs_recovery: exact size read failed: status=0x%lx\n",
			status);
		return -EIO;
	}
	if (!(attributes & EFI_VARIABLE_RUNTIME_ACCESS)) {
		pr_warn("insyde_efivarfs_recovery: enumerated variable lacks EFI_VARIABLE_RUNTIME_ACCESS\n");
		return -EINVAL;
	}
	if (data_size > MAX_LFS_FILESIZE - sizeof(u32))
		return -EOVERFLOW;

	variable = kzalloc(struct_size(variable, name,
				       name_size / sizeof(*name)), GFP_KERNEL);
	if (!variable)
		return -ENOMEM;

	variable->guid = *guid;
	variable->name_size = name_size;
	variable->data_size = data_size;
	variable->attributes = attributes;
	memcpy(variable->name, name, name_size);
	list_add_tail(&variable->link, variables);
	*added = true;
	return 0;
}

static int collect_seeded_variables(struct list_head *variables,
				    unsigned int *count,
				    efi_status_t *final_status)
{
	efi_char16_t *name;
	efi_guid_t guid = parsed_seed_guid;
	efi_status_t status;
	unsigned long name_size;
	unsigned long actual_name_size;
	unsigned int next_count = 0;
	bool added;
	int error;

	*count = 0;
	name = kzalloc(RECOVERY_NAME_BYTES, GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	memcpy(name, parsed_seed_name, parsed_seed_name_size);

	error = efivar_lock();
	if (error)
		goto out;

	error = add_exact_variable(variables, parsed_seed_name,
				   parsed_seed_name_size, &parsed_seed_guid,
				   &added);
	if (error)
		goto unlock;
	if (!added) {
		error = -ELOOP;
		goto unlock;
	}
	if (!(list_first_entry(variables, struct recovered_variable, link)->attributes &
	      EFI_VARIABLE_RUNTIME_ACCESS)) {
		pr_err("insyde_efivarfs_recovery: seed lacks EFI_VARIABLE_RUNTIME_ACCESS\n");
		error = -EINVAL;
		goto unlock;
	}
	(*count)++;

	while (next_count < RECOVERY_COUNT_LIMIT) {
		name_size = RECOVERY_NAME_BYTES;
		status = efivar_get_next_variable(&name_size, name, &guid);
		if (status != EFI_SUCCESS)
			break;

		actual_name_size = bounded_name_size(name, name_size);
		if (!actual_name_size) {
			status = EFI_INVALID_PARAMETER;
			break;
		}

		error = add_exact_variable(variables, name, actual_name_size, &guid,
					   &added);
		if (error)
			break;
		if (!added) {
			pr_err("insyde_efivarfs_recovery: seeded enumeration returned a duplicate variable\n");
			error = -ELOOP;
			break;
		}
		next_count++;
		(*count)++;
	}

	*final_status = status;
	if (next_count == RECOVERY_COUNT_LIMIT)
		error = -ELOOP;
unlock:
	efivar_unlock();
out:
	kfree(name);
	return error;
}

static bool name_is_ascii(const efi_char16_t *name)
{
	unsigned int index;

	for (index = 0; index < EFI_VAR_NAME_LEN / sizeof(*name); index++) {
		if (!name[index])
			return index != 0;
		if (name[index] > 0x7f)
			return false;
	}

	return false;
}

static char *make_variable_path(const struct recovered_variable *variable)
{
	char guid_text[EFI_VARIABLE_GUID_LEN + 1];
	char *path;
	char *cursor;
	size_t path_size;
	size_t name_length;
	unsigned int index;

	if (!name_is_ascii(variable->name))
		return ERR_PTR(-EILSEQ);

	name_length = ucs2_utf8size(variable->name);
	path_size = strlen(efivarfs_path) + 1 + name_length + 1 +
		    EFI_VARIABLE_GUID_LEN + 1;
	path = kmalloc(path_size, GFP_KERNEL);
	if (!path)
		return ERR_PTR(-ENOMEM);

	cursor = path;
	cursor += scnprintf(cursor, path_size, "%s/", efivarfs_path);
	ucs2_as_utf8(cursor, variable->name, name_length);
	for (index = 0; index < name_length; index++) {
		if (cursor[index] == '/')
			cursor[index] = '!';
	}
	cursor += name_length;
	*cursor++ = '-';
	efi_guid_to_str((efi_guid_t *)&variable->guid, guid_text);
	memcpy(cursor, guid_text, sizeof(guid_text));
	return path;
}

static int prepare_variable_paths(struct list_head *variables,
				  unsigned int *skipped_count)
{
	struct recovered_variable *variable;

	*skipped_count = 0;
	list_for_each_entry(variable, variables, link) {
		if (!name_is_ascii(variable->name)) {
			(*skipped_count)++;
			continue;
		}

		variable->path = make_variable_path(variable);
		if (IS_ERR(variable->path)) {
			int error = PTR_ERR(variable->path);

			variable->path = NULL;
			return error;
		}
	}

	return 0;
}

static int set_file_size(struct file *file, unsigned long data_size)
{
	struct inode *inode = file_inode(file);

	inode_lock(inode);
	i_size_write(inode, data_size + sizeof(u32));
	inode_unlock(inode);
	return 0;
}

static int expose_one_variable(const struct recovered_variable *variable,
			       bool *created)
{
	struct file *file;
	int error;

	*created = false;

	/*
	 * O_CREAT makes only an in-memory efivarfs inode.  No write follows, so
	 * efivarfs never calls SetVariable().  Setting i_size prevents efivarfs
	 * from treating the entry as an abandoned zero-length creation on close.
	 */
	file = filp_open(variable->path, O_RDONLY | O_CREAT | O_EXCL, 0644);
	if (!IS_ERR(file)) {
		set_file_size(file, variable->data_size);
		filp_close(file, NULL);
		*created = true;
		error = 0;
		goto out;
	}

	error = PTR_ERR(file);
	if (error != -EEXIST)
		goto out;

	/* Repair a zero-sized entry left by an interrupted earlier run. */
	file = filp_open(variable->path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto out;
	}
	set_file_size(file, variable->data_size);
	filp_close(file, NULL);
	error = 0;
out:
	return error;
}

static int validate_efivarfs_mount(bool *read_only)
{
	struct path path;
	int error;

	error = kern_path(efivarfs_path, LOOKUP_FOLLOW, &path);
	if (error)
		return error;
	if (path.dentry->d_sb->s_magic != EFIVARFS_MAGIC)
		error = -EINVAL;
	*read_only = sb_rdonly(path.dentry->d_sb);
	path_put(&path);
	return error;
}

static void free_variables(struct list_head *variables)
{
	struct recovered_variable *variable;
	struct recovered_variable *next;

	list_for_each_entry_safe(variable, next, variables, link) {
		list_del(&variable->link);
		kfree(variable->path);
		kfree(variable);
	}
}

static int prepare_seed(void)
{
	size_t index;
	size_t length;

	if (!seed_name || !seed_guid)
		return -EINVAL;

	length = strnlen(seed_name, ARRAY_SIZE(parsed_seed_name));
	if (!length || length >= ARRAY_SIZE(parsed_seed_name))
		return -ENAMETOOLONG;

	for (index = 0; index < length; index++) {
		unsigned char value = seed_name[index];

		if (value < 0x20 || value > 0x7e)
			return -EILSEQ;
		parsed_seed_name[index] = value;
	}
	parsed_seed_name[length] = 0;
	parsed_seed_name_size = (length + 1) * sizeof(*parsed_seed_name);

	if (guid_parse(seed_guid, &parsed_seed_guid))
		return -EINVAL;

	return 0;
}

static int __init insyde_efivarfs_recovery_init(void)
{
	LIST_HEAD(variables);
	struct recovered_variable *variable;
	efi_status_t normal_status = EFI_SUCCESS;
	efi_status_t seeded_status = EFI_SUCCESS;
	unsigned int normal_count;
	unsigned int recovered_count;
	unsigned int created_count = 0;
	unsigned int present_count = 0;
	unsigned int failed_count = 0;
	unsigned int skipped_count = 0;
	bool seed_seen;
	bool mount_read_only = false;
	bool created;
	int error;

	pr_info("insyde_efivarfs_recovery: starting %s scan\n",
		expose ? "recovery" : "read-only dry-run");

	if (!efivar_is_available())
		return -EOPNOTSUPP;

	error = prepare_seed();
	if (error) {
		pr_err("insyde_efivarfs_recovery: invalid seed_name or seed_guid: %d\n",
		       error);
		return error;
	}
	pr_info("insyde_efivarfs_recovery: seed=%s-%s\n", seed_name, seed_guid);

	error = inspect_normal_enumeration(&normal_count, &seed_seen,
					   &normal_status);
	if (error) {
		pr_err("insyde_efivarfs_recovery: normal enumeration failed: %d\n",
		       error);
		return error;
	}

	pr_info("insyde_efivarfs_recovery: empty-start count=%u final=0x%lx seed=%s\n",
		normal_count, normal_status, seed_seen ? "present" : "absent");

	if (normal_status != EFI_NOT_FOUND) {
		pr_err("insyde_efivarfs_recovery: empty-start walk did not end with EFI_NOT_FOUND; no action\n");
		return -ENODEV;
	}

	if (normal_count > normal_count_limit || seed_seen) {
		pr_info("insyde_efivarfs_recovery: collapse signature not present; no action\n");
		return 0;
	}

	error = collect_seeded_variables(&variables, &recovered_count,
					 &seeded_status);
	if (error) {
		pr_err("insyde_efivarfs_recovery: seeded enumeration failed: %d, status=0x%lx\n",
		       error, seeded_status);
		free_variables(&variables);
		return error;
	}

	pr_info("insyde_efivarfs_recovery: seeded count=%u final=0x%lx\n",
		recovered_count, seeded_status);
	if (seeded_status != EFI_NOT_FOUND || recovered_count <= normal_count) {
		pr_err("insyde_efivarfs_recovery: seeded result failed safety checks; no action\n");
		free_variables(&variables);
		return -ENODEV;
	}

	if (!expose) {
		pr_info("insyde_efivarfs_recovery: dry run complete; load with expose=1 to create entries\n");
		free_variables(&variables);
		return 0;
	}

	error = validate_efivarfs_mount(&mount_read_only);
	if (error) {
		pr_err("insyde_efivarfs_recovery: %s is not a usable efivarfs mount: %d\n",
		       efivarfs_path, error);
		free_variables(&variables);
		return error;
	}
	if (mount_read_only) {
		pr_err("insyde_efivarfs_recovery: %s is read-only; remount it read-write first\n",
		       efivarfs_path);
		free_variables(&variables);
		return -EROFS;
	}

	error = prepare_variable_paths(&variables, &skipped_count);
	if (error) {
		pr_err("insyde_efivarfs_recovery: could not prepare all efivarfs paths: %d; no action\n",
		       error);
		free_variables(&variables);
		return error;
	}

	list_for_each_entry(variable, &variables, link) {
		if (!variable->path)
			continue;
		error = expose_one_variable(variable, &created);
		if (error) {
			failed_count++;
			pr_warn("insyde_efivarfs_recovery: entry creation failed: %d\n",
				error);
		} else if (created) {
			created_count++;
		} else {
			present_count++;
		}
	}

	pr_info("insyde_efivarfs_recovery: complete recovered=%u created=%u present=%u skipped_non_ascii=%u failed=%u\n",
		recovered_count, created_count, present_count, skipped_count,
		failed_count);
	free_variables(&variables);
	return failed_count ? -EIO : 0;
}

static void __exit insyde_efivarfs_recovery_exit(void)
{
	pr_info("insyde_efivarfs_recovery: unloaded; created efivarfs entries remain until unmount or reboot\n");
}

module_init(insyde_efivarfs_recovery_init);
module_exit(insyde_efivarfs_recovery_exit);

MODULE_DESCRIPTION("Recover efivarfs enumeration through a validated variable seed");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EFIVAR");
