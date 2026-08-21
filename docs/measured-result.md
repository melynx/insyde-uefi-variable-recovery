# Measured results

I measured all of the numbers below on my Lenovo Yoga Slim 7i Aura Edition.
Another retail unit suffered the same issue, but I did not use it for these
measurements.

The results are specific to this machine. They do not mean that every Insyde
system has the same variable names or store layout.

## UEFI runtime transition

I used a single UEFI application to enumerate the variables, call
`ExitBootServices()`, and enumerate them again before any operating system
ran:

```text
before ExitBootServices(): 168 variables
ExitBootServices():        EFI_SUCCESS
after ExitBootServices():   15 variables
```

No operating system ran between those two walks. So the variables did not
vanish because Linux did something to them. The list collapsed inside the
firmware runtime transition.

## Exact reads and seeded enumeration

The Linux runtime tests returned:

```text
empty-name enumeration:        15 variables
GetVariable(PlatformLang):     success
PlatformLang-seeded walk:     152 variables
GetVariable(SetupMode):        success
GetVariable(BootOrder):        success
```

The seeded walk includes both the hidden main-store names and the fallback
names.

## `efivarfs` exposure

The signed proof-of-concept module reported:

```text
recovered:       152
created:         136
already present: 16
failed:            0
```

After exposure, `efibootmgr` could see `BootOrder` and `Boot####` again.
`sbctl` could read `SetupMode` and `SecureBoot` again.

## Later 158-variable result

A later service test found 158 names, created 143 missing entries, and found
15 already present. The variable store had changed between the two captures.

The exact total is not stable because UEFI variables can be added or deleted.
What matters is the shape of the failure: the normal empty-name walk returned
only 15 names, while the seeded walk returned the much larger hidden list and
ended normally.

## NVRAM record order

After the Secure Boot key records were deleted, the first live records were:

```text
AbtSetup     attributes 0x03 (NV+BS)
AbtState     attributes 0x03 (NV+BS)
PlatformLang attributes 0x07 (NV+BS+RT)
```

`PlatformLang` was the first live record with
`EFI_VARIABLE_RUNTIME_ACCESS` on this machine. Another machine can have a
different name in that position, so do not blindly copy `PlatformLang`. Use
the NVRAM parser or the UEFI probe, then pass the confirmed `seed_name` and
`seed_guid` to the module.
