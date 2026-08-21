# `insyde_efivarfs_recovery`

`insyde_efivarfs_recovery` does one specific thing. It works around the broken
firmware enumeration after Linux has booted.

Module parameters:

- `seed_name`: ASCII UEFI variable name. Default: `PlatformLang`.
- `seed_guid`: canonical GUID for `seed_name`. Default: the UEFI global
  variable GUID.
- `normal_count_limit`: largest empty-start list treated as collapsed.
  Default: 32.
- `efivarfs_path`: mounted `efivarfs` path. Default:
  `/sys/firmware/efi/efivars`.
- `expose`: create missing in-memory entries. Default: false.

The module rejects a seed unless exact `GetVariable()` succeeds and returns
`EFI_VARIABLE_RUNTIME_ACCESS`. It rejects recovery unless the seeded walk is
larger than the empty-start walk and ends with `EFI_NOT_FOUND`.

The module exposes ASCII variable names. It counts and skips non-ASCII names
because creating those names through the public `efivarfs` file interface
would encode them incorrectly. It also rejects duplicate enumeration results,
an abnormal walk end, and any variable returned without
`EFI_VARIABLE_RUNTIME_ACCESS`.

Start dry. Read the kernel log. Unload the module. Use `expose=1` only when the
small empty-start count and the larger seeded count prove the expected
failure.

`expose=1` also requires a read-write `efivarfs` mount. The complete build,
module-signing, dry-run, exposure, key-enrollment, and NVRAM-seed instructions
are in [`../docs/usage.md`](../docs/usage.md).

The module accepts ASCII seed names because Linux module parameters arrive as
byte strings. The NVRAM parser skips non-ASCII candidates.
