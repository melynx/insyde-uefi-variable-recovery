# Insyde UEFI variable enumeration recovery

## Lenovo support

I tried to enroll my own Secure Boot keys on a Lenovo Yoga Slim 7i Aura
Edition. The BIOS provides a `Reset to Setup Mode` option, so this should have
been routine.

Instead, entering Setup Mode made most UEFI variables disappear from Linux.
`sbctl`, `efibootmgr`, and other tools stopped working. Restoring the factory
keys made the variables appear again. Another retail unit suffered the same
issue.

That started more than a month and a half of back and forth with Lenovo, with
very little effort spent on finding out what actually went wrong. Lenovo
technical support told me that the Yoga was a consumer product, that this kind
of control was an enterprise feature intended for ThinkPads, and that what I
was doing amounted to "forensic-level usage."

Which is funny, because Lenovo's own cheaper consumer IdeaPad performs the same
enrollment correctly.

I pointed that out. Lenovo then closed the case as "Solution Provided."
Nothing was fixed.

I reopened the case and pushed it from technical support to Customer Care.
Lenovo's "BIOS engineering team" finally responded through the Customer Care
agent. This time, custom key enrollment supposedly did work. They sent me an
`sbctl` procedure I had already shown failing, followed by a video of the same
procedure working on their engineering machine.

So we went from "this is an enterprise feature" and "forensic-level usage" to
"it works on our machine." Very consistent.

Lenovo then told me to reinstall the BIOS. There was only one small problem: I
was already running the exact same BIOS version shown in their test. I forced
the same BIOS to flash again anyway, proved that the flash completed, and
confirmed that the bug was still there.

The whole process felt like gaslighting. Every answer ignored evidence I had
already provided and replaced the previous explanation with a new one. Each
response also took about a week to arrive, so this went on for another three to
four weeks without Lenovo getting any closer to the cause.

After more than a month and a half of this ordeal, I gave up waiting and
reverse-engineered the BIOS myself (my years of PhD finally came into use). I
dumped the UEFI variable store, wrote UEFI probes and a Linux kernel module,
and disassembled Insyde's `VariableRuntimeDxe` driver. I found the actual
firmware bug and built a working workaround.

I told Lenovo what was broken. I told them that I understood the bug, had a
working workaround, and was happy to speak directly with their engineers to
help them produce a proper BIOS fix. Lenovo chose not to contact me. Another
two weeks passed without any meaningful technical response.

Only after all of this did Lenovo claim that its team was "fixing the kernel."
The failure happens inside the firmware before Linux is involved, so good luck
with that.

Well, at least Lenovo is claiming that the team is working with Insyde to fix
the BIOS. Anyway, that's the reason for me creating this repo: to make public
the reason why the BIOS is broken and how to work around it. It might be
helpful for folks trying to pressure manufacturers to actually fix it.

The longer version is in [Lenovo support made me reverse engineer my own
BIOS](docs/lenovo-support-postmortem.md).

The disassembly and exact repair target are in
[Lenovo QSCN33WW `VariableRuntimeDxe` analysis](docs/lenovo-bios-analysis.md).

## Firmware bug

The UEFI variables did not disappear. The firmware simply stopped listing most
of them.

UEFI has two calls that matter here. `GetVariable()` reads a variable when the
caller already knows its name and GUID. `GetNextVariableName()` walks the list
when the caller does not know those names yet.

After `ExitBootServices()`, the Insyde firmware on the two affected Yoga
machines breaks the second call. Linux appears to lose `SetupMode`,
`SecureBoot`, `BootOrder`, `Boot####`, `PK`, `KEK`, `db`, and `dbx`. Exact
`GetVariable()` calls can still read them. The data remains in firmware
storage. The firmware just prevents Linux from discovering the names.

## Repository layout

The repository has:

- `kernel/`: a loadable Linux module that walks the hidden list from a known
  runtime variable and creates the missing in-memory `efivarfs` entries;
- `systemd/`: an optional one-shot service that runs recovery after `efivarfs`
  is mounted and restores its original read-only state;
- `efi/`: a read-only UEFI application that records variable order and finds a
  possible runtime seed before an operating system starts;
- `tools/dump_nvram.py`: a read-only copier for a live SPI flash device such as
  `/dev/mtd0ro`;
- `tools/nvram_records.py`: a parser that lists physical variable records and
  selects the first live record with `EFI_VARIABLE_RUNTIME_ACCESS`;
- `tools/recover_from_nvram.py`: turns the parser result into kernel module
  parameters. It prints a command by default and loads the module only with
  `--load`;
- `docs/`: the incident history, failure mechanism, Lenovo BIOS disassembly,
  safety limits, measurements, and Framework BIOS analysis.

## Linux workaround

Linux normally starts `GetNextVariableName()` with an empty name. The affected
firmware checks the first live record in the main store. If that one record is
not available at runtime, the firmware abandons the entire store instead of
skipping the record and trying the next one. Linux ends up seeing only a small
fallback list.

The module starts a second walk from a known runtime variable in the hidden
store. It first checks that an exact `GetVariable()` call can read the seed and
that the seed has `EFI_VARIABLE_RUNTIME_ACCESS`. It then requires the seeded
walk to return more variables than the broken empty-name walk and to end
normally. Only after those checks pass can `expose=1` create the missing
in-memory `efivarfs` entries. The module never calls firmware `SetVariable()`.

## Limits

This code does not repair firmware. The Linux workaround lasts until reboot or
until `efivarfs` is unmounted. Windows and pre-boot software remain affected.

The default kernel-module mode is a dry run. It calls only `GetVariable()` and
`GetNextVariableName()`. It never calls `SetVariable()`.

## Build

Build the Linux module against the running kernel:

```sh
make -C kernel
```

When Secure Boot enforces signed modules, sign `kernel/insyde_efivarfs_recovery.ko`
with a key trusted by the running kernel. This repository never stores signing
keys.

Build the UEFI application with GNU-EFI:

```sh
make -C efi
```

The result is:

```text
efi/build/uefi-var-probe.efi
```

The application is read-only. It records variable names, GUIDs, attributes,
sizes, and enumeration order. It does not record variable contents.

## Default seed

The measured machine used `PlatformLang` and the UEFI global variable GUID as
the first usable runtime seed. Start with a dry run:

```sh
sudo insmod kernel/insyde_efivarfs_recovery.ko
dmesg | tail -n 20
sudo rmmod insyde_efivarfs_recovery
```

If the log shows a small empty-start list and a larger seeded list, check
whether `efivarfs` is read-only:

```sh
findmnt -no TARGET,FSTYPE,OPTIONS /sys/firmware/efi/efivars
```

Remount it read-write when needed, then expose the missing entries:

```sh
sudo mount -o remount,rw /sys/firmware/efi/efivars
sudo insmod kernel/insyde_efivarfs_recovery.ko expose=1
sudo dmesg | grep insyde_efivarfs_recovery | tail -n 20
sudo rmmod insyde_efivarfs_recovery
```

Verify the recovered view:

```sh
efibootmgr
sbctl status
```

The module may need a signature from a key trusted by the running kernel.
Secure Boot key enrollment also requires every necessary EFI boot file to be
signed first. See [Using the Linux workaround](docs/usage.md) for the complete
workflow, module-signing commands, safety checks, `sbctl` steps, and reboot
limitations.

## Finding a seed on another machine

NVRAM here means the non-volatile UEFI variable store inside SPI flash. Linux
also has a device called `/dev/nvram`, but that normally exposes the small CMOS
device. It is not the UEFI variable store we want.

First copy a read-only SPI flash device. The output may contain private machine
data, so do not publish it:

```sh
sudo python3 tools/dump_nvram.py /dev/mtd0ro nvram.bin
```

List stores and records without printing variable data:

```sh
python3 tools/nvram_records.py nvram.bin
```

Select the first live runtime record and print the module command:

```sh
python3 tools/recover_from_nvram.py nvram.bin \
  kernel/insyde_efivarfs_recovery.ko
```

Run the dry test only after reviewing the selected name, GUID, attributes, and
store offset:

```sh
sudo python3 tools/recover_from_nvram.py nvram.bin \
  kernel/insyde_efivarfs_recovery.ko --load
```

Add `--expose` only after the dry run succeeds.

The full procedure is in [Using the Linux workaround](docs/usage.md).

The parser understands the standard and authenticated EDK II record headers.
It checks both packed and four-byte-aligned record chains. Vendor firmware can
still use another layout, so the parser result is only a candidate. Do not
blindly trust it. The kernel module must confirm the candidate with an exact
`GetVariable()` call and a complete seeded walk before it creates any
`efivarfs` entry.

## Related reports

- [Framework issue #30](https://github.com/FrameworkComputer/SoftwareFirmwareIssueTracker/issues/30)
- [Framework issue #76](https://github.com/FrameworkComputer/SoftwareFirmwareIssueTracker/issues/76)

See `docs/framework-follow-up.md` for the matching code found in the Framework
BIOS images.

## License

GPL-2.0-only. See `LICENSE`.
