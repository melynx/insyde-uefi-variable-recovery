# Lenovo support made me reverse engineer my own BIOS

Entering Secure Boot Setup Mode made most UEFI variables disappear on my Yoga
Slim 7i Aura Edition. Another retail unit suffered the same issue. Lenovo then
spent more than a month and a half giving me slow, contradictory answers. I
eventually reverse-engineered the BIOS, found the bug, built a workaround, and
offered the result back to Lenovo. Lenovo did not take me up on that offer.

So here we are.

## Secure Boot Setup Mode

My affected machine is a Lenovo Yoga Slim 7i Aura Edition, also identified as
the Yoga Slim 7 14ILL10, machine type `83JX`. It runs Insyde BIOS `QSCN33WW`.
The detailed measurements in this document come from my machine.

I wanted to enroll my own Secure Boot keys with `sbctl`. The BIOS provides a
`Reset to Setup Mode` option, so this should have been fairly boring:

1. clear the existing Platform Key;
2. boot Linux in Setup Mode;
3. create and enroll my own keys; and
4. enable Secure Boot again.

Instead, entering Setup Mode made Linux lose most of its UEFI variable files.
The missing names included:

- `SetupMode` and `SecureBoot`;
- `PK`, `KEK`, `db`, and `dbx`;
- `BootOrder`, `BootCurrent`, and the `Boot####` entries; and
- `OsIndicationsSupported`.

Several unrelated tools failed together. `sbctl` claimed that the machine was
not booted with UEFI. `efibootmgr` could not find `BootOrder`. `bootctl` could
not see the normal Secure Boot state.

Restoring the factory keys made the variables appear again. Entering Setup
Mode made them disappear again.

That should have been the first clue that this was a firmware-state problem,
not a Linux configuration problem.

## Lenovo technical support

I reported the problem to Lenovo technical support. The response was that the
Yoga was a consumer product, this kind of customization belonged on enterprise
ThinkPads, and what I was doing amounted to "forensic-level usage."

Which is funny, because a cheaper consumer Lenovo IdeaPad performs the exact
same custom-key enrollment correctly.

I sent Lenovo the IdeaPad result. It directly disproved the consumer-versus-
enterprise excuse.

Lenovo then closed the case as "Solution Provided."

No solution was provided. The laptop was unchanged.

## Customer Care escalation

I reopened the matter and pushed it from technical support to Customer Care.
After more waiting, Lenovo's "BIOS engineering team" finally responded through
the Customer Care agent.

This time, the answer changed. Custom Secure Boot key enrollment supposedly did
work on this machine.

Lenovo sent me an `sbctl` procedure that told me to enter Setup Mode and enroll
the keys from Ubuntu. That was the same procedure I had already shown failing.
The problem happened before `sbctl` could even begin enrollment because
`SetupMode` was no longer visible.

Lenovo later sent a video of the procedure working on an engineering machine.
The video showed the same machine type and BIOS version.

So we went from:

- this is an enterprise feature;
- this is forensic-level usage; and
- a Yoga is not meant to do this;

to:

- it works on our Yoga.

Very consistent.

The engineering video did provide one useful result. The same firmware could
work on a different machine. That pointed towards persistent variable-store
state, not the model name or installed BIOS version.

Lenovo did not investigate that difference.

## BIOS reflash

The next answer was to run Lenovo's `QSCN33WW` Windows updater.

There was one small problem: the machine was already running `QSCN33WW`.

I enabled BIOS Back Flash and forced the same version to flash again anyway.
The updater ran. The firmware consumed the payload. The system still reported
`QSCN33WW` afterwards.

And the bug was still there.

A normal BIOS reflash updates firmware code. It does not necessarily rebuild
the persistent UEFI variable store. Reinstalling the same code therefore left
the record order that triggered the bug untouched.

## Support delays

Every new answer took about a week to arrive. Each answer also ignored evidence
I had already provided and replaced the previous explanation with another one.

First the feature was unsupported. Then it worked. Then I was following the
wrong procedure. Then reinstalling the BIOS I already had was supposed to fix
it.

The process felt like gaslighting. It dragged on for another three to four
weeks without Lenovo getting any closer to the cause.

Eventually I gave up waiting.

## Reverse engineering

After more than a month and a half, I started treating the laptop like the
firmware-debugging project Lenovo had accidentally volunteered it to become.

So I:

1. extracted the BIOS update;
2. dumped the live UEFI variable store;
3. compared the healthy and broken states;
4. wrote UEFI applications to test enumeration before and after
   `ExitBootServices()`;
5. wrote Linux kernel probes for exact `GetVariable()` calls and seeded
   `GetNextVariableName()` walks; and
6. disassembled Insyde's `VariableRuntimeDxe` driver.

That finally answered the question Lenovo had spent weeks avoiding.

The variables were not gone. Exact `GetVariable()` calls could still read
them. The firmware had stopped listing their names through
`GetNextVariableName()`.

A single UEFI application measured the boundary without Linux running between
the two tests:

```text
before ExitBootServices(): 168 variables
after ExitBootServices():   15 variables
```

## Firmware bug

At runtime, firmware must skip variables that do not have
`EFI_VARIABLE_RUNTIME_ACCESS`. That part is normal.

The affected Insyde code checks the first live record in the main variable
store. If that one record is not available at runtime, the code abandons the
entire main store and switches to a small fallback store. It should skip the
record and continue to the next one.

On this machine, the first live records after the Secure Boot keys were
cleared were:

```text
AbtSetup      attributes 0x03: non-volatile, boot-service access
AbtState      attributes 0x03: non-volatile, boot-service access
PlatformLang  attributes 0x07: non-volatile, boot-service and runtime access
```

The code sees `AbtSetup`, notices that it lacks runtime access, and throws away
the rest of the main-store search. It never reaches `PlatformLang`, even though
`PlatformLang` is a valid runtime variable immediately after it.

Starting `GetNextVariableName()` from the known `PlatformLang` variable avoids
the broken empty-name path. The seeded walk recovered 152 names. The Linux
module then created 136 missing in-memory `efivarfs` entries without calling
firmware `SetVariable()`.

After that, `efibootmgr` and `sbctl` worked again.

The complete mechanism and safety checks are documented in
[Failure mechanism](mechanism.md), [Measured result](measured-result.md), and
[Using the Linux workaround](usage.md).

## Lenovo's final response

I told Lenovo what was broken. I told them that I understood the cause, had a
working workaround, and was happy to speak directly with their engineers to
help them produce a proper BIOS fix.

Lenovo chose not to contact me.

Another two weeks passed without any meaningful technical response. Only
after all of that, Lenovo claimed that its team was "fixing the kernel," even
though the failure happens inside firmware before Linux is involved.

So much for fixing the kernel.

Well, at least Lenovo now claims that its team is working with Insyde on a
final solution. Hopefully that means a corrected BIOS.

## Publishing the evidence

I am no longer willing to let the technical facts remain hidden inside a
private support case.

I am publishing the cause, measurements, probes, and Linux workaround. This
also gives owners who suspect the same Insyde bug a way to test for it without
pretending that the Lenovo result proves their machine is affected.

Maybe it will help other folks pressure manufacturers into actually fixing the
firmware they ship.

## Firmware fix

The real fix is small in concept: when the first live record lacks
`EFI_VARIABLE_RUNTIME_ACCESS`, `VariableRuntimeDxe` must continue to the next
record instead of abandoning the main variable store.

Lenovo and Insyde should:

1. reproduce the failure with a variable store from an affected retail
   machine, not only a clean engineering unit;
2. fix the initial runtime enumeration path;
3. test Setup Mode after variables have been created and deleted over the life
   of the machine;
4. release a corrected BIOS; and
5. inspect related Insyde firmware families and test each one separately.

Until that happens, this repository is only a workaround. Linux can recover
its view of the variables, but the firmware remains broken.
