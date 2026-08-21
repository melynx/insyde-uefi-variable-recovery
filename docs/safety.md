# Safety and side effects

## Read-only operations

The `uefi-var-probe.efi` application never calls `SetVariable()`.

`dump_nvram.py` opens its source with read-only access. By default, it accepts
only a character-device path whose final name ends in `ro`, such as
`/dev/mtd0ro`. An explicit regular-file input is also allowed for testing.

`nvram_records.py` never changes its input. It does not print variable data.

The kernel module uses only `GetVariable()` and `GetNextVariableName()` to talk
to firmware. Its default mode performs no filesystem change.

## `expose=1`

`expose=1` creates missing in-memory names in the mounted `efivarfs` instance.
It does not write variable contents and does not call firmware
`SetVariable()`. The names disappear on reboot or when `efivarfs` is
unmounted.

The module prepares every ASCII filename before creating anything. It counts
and skips non-ASCII names because the public `efivarfs` create interface cannot
recreate those names correctly from UTF-8.

After recovery, a later normal write to one of those files still uses the
normal `efivarfs` write path. That later write can call `SetVariable()`.

## Firmware dumps

A full SPI dump can contain device identifiers, firmware settings, network
configuration, and other private data. Keep it private. The repository ignores
common firmware-image extensions, but still check `git status` before every
commit.

Never store a Secure Boot private key or module-signing private key in this
repository.

## Out of scope

These tools do not patch or flash firmware. They do not modify raw SPI flash.
They do not try to recreate a damaged variable store. Stop if the parser cannot
identify a clean record chain.
