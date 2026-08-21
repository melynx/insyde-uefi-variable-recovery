# Linux workaround

This workaround recovers the UEFI variable names hidden by the Insyde runtime
enumeration bug.

The workaround changes only the current Linux `efivarfs` mount. It does not
patch firmware. It does not write raw SPI flash. Rebooting or unmounting
`efivarfs` removes the recovered filenames.

## Confirm the failure pattern

Use this module only when all of these symptoms occur together:

- the machine booted through UEFI;
- `/sys/firmware/efi/efivars` exists;
- only a small number of variables are visible;
- normal variables such as `SetupMode` or `BootOrder` are missing; and
- restoring factory Secure Boot keys makes the variables visible again.

Check the current count and the two common missing names:

```sh
find /sys/firmware/efi/efivars -mindepth 1 -maxdepth 1 | wc -l

test -e /sys/firmware/efi/efivars/SetupMode-8be4df61-93ca-11d2-aa0d-00e098032b8c \
  && echo "SetupMode is visible" \
  || echo "SetupMode is missing"

test -e /sys/firmware/efi/efivars/BootOrder-8be4df61-93ca-11d2-aa0d-00e098032b8c \
  && echo "BootOrder is visible" \
  || echo "BootOrder is missing"
```

Do not use the module when normal enumeration already works. The module also
checks for this condition and refuses recovery.

## 1. Build the module

Install the compiler, `make`, and the kernel build files matching the running
kernel. Then run from the repository root:

```sh
make -C kernel
```

The result is:

```text
kernel/insyde_efivarfs_recovery.ko
```

Rebuild the module after every kernel update.

## 2. Sign the module when required

Some kernels reject every unsigned module. Check the running kernel
configuration when it is available:

```sh
grep '^CONFIG_MODULE_SIG_FORCE=' /boot/config-"$(uname -r)" 2>/dev/null || true
```

If the result is `CONFIG_MODULE_SIG_FORCE=y`, sign the module with a
certificate trusted by the running kernel.

For an `sbctl` installation whose `db` certificate is trusted for kernel
modules, the usual command is:

```sh
sudo /lib/modules/"$(uname -r)"/build/scripts/sign-file sha512 \
  /var/lib/sbctl/keys/db/db.key \
  /var/lib/sbctl/keys/db/db.pem \
  kernel/insyde_efivarfs_recovery.ko
```

Owning the Secure Boot `db` key does not guarantee that every Linux
distribution accepts that certificate for kernel modules. If `insmod` reports
`Key was rejected by service`, stop and follow the distribution's module-key
enrollment process. Do not bypass signature checking with a forced load.

Running `make -C kernel` again replaces the signed module with a new unsigned
one. Sign every rebuilt module.

## 3. Start with the read-only test

The default seed is `PlatformLang` under the UEFI global-variable GUID. This
seed was confirmed on the original Lenovo machine.

Do not jump straight to `expose=1`. Load the module without it first:

```sh
sudo insmod kernel/insyde_efivarfs_recovery.ko
sudo dmesg | grep insyde_efivarfs_recovery | tail -n 20
sudo rmmod insyde_efivarfs_recovery
```

The dry run calls only `GetVariable()` and `GetNextVariableName()`. It does not
create files and does not call firmware `SetVariable()`.

Look for output like this:

```text
empty-start count=15 ... seed=absent
seeded count=152 ...
dry run complete
```

The exact counts can change. That is fine. These are the conditions that
matter:

- the empty-start count is small;
- the default enumeration does not contain the seed;
- exact `GetVariable()` can read the seed;
- the seed has `EFI_VARIABLE_RUNTIME_ACCESS`;
- the seeded count is larger than the empty-start count; and
- the seeded walk ends normally with `EFI_NOT_FOUND`.

The module checks every condition. It creates nothing if a check fails.

If `PlatformLang` does not work, do not guess another name. Follow
[Finding a seed on another machine](#finding-a-seed-on-another-machine).

## 4. Expose the missing filenames

Check whether `efivarfs` is read-only:

```sh
findmnt -no TARGET,FSTYPE,OPTIONS /sys/firmware/efi/efivars
```

If the options contain `ro`, temporarily remount it read-write:

```sh
sudo mount -o remount,rw /sys/firmware/efi/efivars
```

Now run the recovery pass:

```sh
sudo insmod kernel/insyde_efivarfs_recovery.ko expose=1
sudo dmesg | grep insyde_efivarfs_recovery | tail -n 20
sudo rmmod insyde_efivarfs_recovery
```

Look for output like this:

```text
complete recovered=152 created=136 present=16 skipped_non_ascii=0 failed=0
```

The module creates only missing in-memory `efivarfs` filenames. It does not
write their contents. It therefore does not call firmware `SetVariable()`
during recovery. It counts and skips non-ASCII variable names because the
public `efivarfs` create interface does not decode a UTF-8 filename back into
the original UEFI name.

Unloading the module does not remove the files. They remain until reboot or
until `efivarfs` is unmounted.

## 5. Verify the result

```sh
ls /sys/firmware/efi/efivars/SetupMode-8be4df61-93ca-11d2-aa0d-00e098032b8c
ls /sys/firmware/efi/efivars/BootOrder-8be4df61-93ca-11d2-aa0d-00e098032b8c

efibootmgr
sbctl status
```

`efibootmgr` should show `BootCurrent`, `BootOrder`, and the `Boot####`
entries. `sbctl status` should recognize the UEFI system and report the real
Setup Mode and Secure Boot state.

## 6. Optional: enroll Secure Boot keys

The recovery itself does not require key enrollment. Stop after the previous
step if the goal was only to restore read access for Linux tools.

Before enabling Secure Boot with owner keys, check the EFI executables on the
EFI System Partition:

```sh
sudo sbctl verify
```

Sign every EFI executable that the machine needs to boot. Review each path
before signing it. For example:

```sh
sudo sbctl sign -s /path/to/required-file.efi
sudo sbctl verify
```

`sbctl sign` replaces the file unless an output path is supplied. Keep a
working recovery USB before changing the Secure Boot trust chain.

Choose the enrollment policy deliberately:

```sh
# Enroll only the owner keys managed by sbctl.
sudo sbctl enroll-keys

# Also include Microsoft keys.
sudo sbctl enroll-keys --microsoft

# Also include Microsoft keys and the firmware's built-in keys.
sudo sbctl enroll-keys --microsoft --firmware-builtin
```

Do not copy the last command unless those additional trust anchors are wanted.
Do not use `--yes-this-might-brick-my-machine` to bypass an enrollment error.

After enrollment:

```sh
sbctl status
sudo sbctl verify
```

A later normal write to a recovered `efivarfs` file uses the normal Linux
`efivarfs` path and can call firmware `SetVariable()`. The read-only guarantee
applies to the recovery module, not to commands such as `sbctl enroll-keys`.

If `efivarfs` was intentionally read-only before recovery, restore that state
only after all intended firmware writes are complete:

```sh
sudo mount -o remount,ro /sys/firmware/efi/efivars
```

## 7. Repeat after reboot

The custom Platform Key can be written near the end of the variable store. It
does not necessarily move the boot-service-only record that triggers this bug.
The firmware can therefore enforce the newly enrolled Secure Boot keys while
runtime enumeration remains broken.

Run the dry test and recovery pass again after reboot. An automatic service
must run only after `efivarfs` is mounted, and the module must be rebuilt and
signed after each kernel update. A systemd one-shot service that preserves the
original `efivarfs` mount state is provided in [`../systemd/`](../systemd/).

## Finding a seed on another machine

Do not assume that another machine can use `PlatformLang`. The seed must be a
live variable in the hidden main store with
`EFI_VARIABLE_RUNTIME_ACCESS`.

### SPI dump and NVRAM parser

NVRAM here means the non-volatile UEFI variable store in SPI flash. Linux
`/dev/nvram` is normally the small CMOS device and is not the correct source.

If Linux exposes a read-only SPI device such as `/dev/mtd0ro`, copy it:

```sh
sudo python3 tools/dump_nvram.py /dev/mtd0ro nvram.bin
```

The dump can contain device identifiers, firmware settings, and network data.
Keep it private.

List the variable records without printing their values:

```sh
python3 tools/nvram_records.py nvram.bin
```

Select the first live runtime record and print the proposed module command:

```sh
python3 tools/recover_from_nvram.py nvram.bin \
  kernel/insyde_efivarfs_recovery.ko
```

Review the selected record name, GUID, attributes, and store offset. The script
does not load anything unless `--load` is present.

Run the read-only module test:

```sh
sudo python3 tools/recover_from_nvram.py nvram.bin \
  kernel/insyde_efivarfs_recovery.ko --load
sudo dmesg | grep insyde_efivarfs_recovery | tail -n 20
sudo rmmod insyde_efivarfs_recovery
```

Only after that test passes, remount `efivarfs` read-write if necessary and run:

```sh
sudo python3 tools/recover_from_nvram.py nvram.bin \
  kernel/insyde_efivarfs_recovery.ko --load --expose
sudo dmesg | grep insyde_efivarfs_recovery | tail -n 20
sudo rmmod insyde_efivarfs_recovery
```

The physical record order gives us a candidate. It does not give us proof.
Firmware can merge multiple stores or use a vendor-specific format. The
module's exact read and seeded walk are the final checks.

### UEFI probe

If no read-only SPI device is available, build the probe:

```sh
make -C efi
```

Copy `efi/build/uefi-var-probe.efi` to a FAT USB drive and start it from the
firmware boot menu or a UEFI shell. It writes `uefi-var-probe.log` beside the
application. The probe never calls `SetVariable()`.

The log records variable attributes and the first enumerated name with
`EFI_VARIABLE_RUNTIME_ACCESS`. Pass a confirmed ASCII name and GUID to the
module:

```sh
sudo insmod kernel/insyde_efivarfs_recovery.ko \
  seed_name=CONFIRMED_NAME \
  seed_guid=00000000-0000-0000-0000-000000000000
```

Run this as a dry test first. Add `expose=1` only after the module reports a
larger seeded list and a normal end status.

## Stop conditions

Stop without exposing files if any of these occur:

- exact `GetVariable()` cannot read the proposed seed;
- the seed lacks `EFI_VARIABLE_RUNTIME_ACCESS`;
- the seeded list is not larger than the empty-start list;
- the seeded walk ends with anything other than `EFI_NOT_FOUND`;
- the NVRAM parser cannot identify a clean record chain; or
- the module reports any failed file creations.

Do not patch raw NVRAM, delete arbitrary variables, or force-load an unsigned
module as part of this workaround.
