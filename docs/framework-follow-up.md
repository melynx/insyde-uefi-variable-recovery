# Framework BIOS images

Framework issues #30 and #76 looked suspiciously similar to the Lenovo bug. In
both reports, UEFI variable access breaks after Secure Boot records are erased
or altered. So I downloaded the related Framework BIOS images and went looking
for the same code in Insyde `VariableRuntimeDxe`.

It is there. The exact Framework Laptop 13 13th Gen Intel Core BIOS 3.08 image
contains the faulty initial-record path described in
[Failure mechanism](mechanism.md).

I only analyzed the BIOS images. I have not reproduced the failure or tested
the workaround on Framework hardware.

## Images

I inspected these Framework BIOS packages:

- Framework Laptop 12, 13th Gen Intel Core, BIOS 3.07;
- Framework Laptop 13, Intel Core Ultra Series 1, BIOS 3.07;
- Framework Laptop 13, 13th Gen Intel Core, BIOS 3.08.

All three images contain the faulty initial-record path and a correct later
loop that skips non-runtime records one at a time. The branch is at image
offset `0x502a` in the Laptop 12 BIOS 3.07 image, `0x4fa6` in the Laptop 13
Intel Core Ultra BIOS 3.07 image, and `0x502a` in the Laptop 13 13th Gen BIOS
3.08 image. The six branch bytes are `0f 85 87 00 00 00` in all three images.

## Framework Laptop 13 13th Gen BIOS 3.08

I downloaded the BIOS 3.08 UEFI updater from
[Framework's official release page](https://community.frame.work/t/framework-laptop-13-13th-gen-intel-core-bios-3-08-release-stable/70435).
Its SHA-256 matched the value published by Framework:

```text
Framework_Laptop_13_13th_Gen_Intel_Core_BIOS_3.08_EFI.zip
42bf2b49fc8dd1e61e34772c60e1ca641d57040e44cbca16460c0998df4eecb4
```

The ZIP contains this signed capsule:

```text
Framework_Laptop_13_13th_Gen_Intel_Core_capsule_signed_allsku_3.08.cap
381fba122a5a819079fc3e811d7bda4baf957989634fe8c26996a41965deec9a
```

UEFIExtract finds `VariableRuntimeDxe` under firmware file GUID
`CBD2E4D5-7068-4FF5-B462-9822B4AD8D60`. The extracted PE32+ x86-64 image is
102,400 bytes and has this SHA-256:

```text
fe7cf28b41c593e2648de735d1f615ea7b12a9d99c7da9245e808c73616b3c27
```

The BIOS 3.08 function starts at image offset `0x4ee4`. Its initial-record
path contains:

```text
0x5007  call 0x3190             find the named record, or the first record
0x5019  call 0x7b2c             test whether runtime mode is active
0x5026  testb $0x4,0x4(%rbx)    test EFI_VARIABLE_RUNTIME_ACCESS
0x502a  jne   0x50b7            keep the record if the bit is set
0x5030  mov   %r12,-0x21(%rbp)  otherwise clear the main-store pointer
0x503d  call 0xe490             call the fallback enumerator
```

The same function contains the correct later loop:

```text
0x5338  call  0x7b2c            test whether runtime mode is active
0x5341  testb $0x4,0x4(%rbx)    test EFI_VARIABLE_RUNTIME_ACCESS
0x5345  je    0x536d            if absent, advance to the next record
0x536d  call  0x10d14           get the next record
0x537c  jmp   0x5147            continue the loop
```

BIOS 3.08 is a different build from the Laptop 12 BIOS 3.07 image, but this
function keeps the same bad branch, at the same offset, with the same
surrounding control flow.

## Public reports

Issue #30 covers the Laptop 13 Intel Core Ultra Series 1 firmware family, and
the reported failure starts after the Secure Boot records are erased. The BIOS
image for that family contains the same bad branch.

The BIOS 3.08 image above is also the exact version named in issue #76. That
issue reports that UEFI variable access failed after an update from BIOS 3.07
to BIOS 3.08 when `dbx` was empty or altered. Again, the faulty path is in the
exact image named in the report.

The BIOS image does not contain the affected machine's live variable store, so
I cannot identify which record triggered the bug on that machine from static
analysis alone.

## Hardware confirmation

To confirm the full path on Framework hardware, I would want:

1. UEFI probe logs before and after `Erase all Secure Boot Settings`;
2. the empty-start count and seeded count from the Linux module dry run;
3. an NVRAM record-order report, if the machine exposes a read-only SPI device,
   to identify the first live runtime record;
4. Insyde review of the empty-name runtime path in `VariableRuntimeDxe`.

The workaround does not repair firmware. Framework or Insyde needs to fix
`GetNextVariableName()`. The function must continue past a first record that
lacks `EFI_VARIABLE_RUNTIME_ACCESS`.

## Public timeline

Honestly, I don't know.

A Framework user was already reporting in August 2023 that their machine
halted only after entering Secure Boot Setup Mode. I cannot prove that the 2023
report had this exact `GetNextVariableName()` root cause. The first public
report I can tie directly to the missing `SetupMode` variable appeared in
December 2024. Framework issue #30 followed a few days later and is still open
in August 2026.

Then Framework released BIOS 3.20 as `STABLE` for the Laptop 13 Ryzen 7040.
The update left some machines unable to boot. Owners were asking for an
official response in Framework's own release thread while Framework's public
response appeared through Ars Technica. Framework later said it would replace
confirmed affected boards, including some outside warranty, and add a recovery
mode in future BIOS releases.

That is better than nothing. But after seeing all of that, I'm not sure
Framework would even bother with this Secure Boot bug. I would be very happy
to be proven wrong.

- [Framework Secure Boot Setup Mode report from August 2023](https://community.frame.work/t/secureboot-setup-mode/14889/18)
- [Framework Laptop 13 Ryzen 7040 BIOS 3.20 `STABLE` release thread](https://community.frame.work/t/framework-laptop-13-ryzen-7040-bios-3-20-release-stable/83283)
- [Ars Technica: Framework responds after BIOS 3.20 bricks Ryzen 7040 laptops](https://arstechnica.com/gadgets/2026/08/framework-responds-to-complaints-that-bios-update-bricked-ryzen-7040-laptops/)
