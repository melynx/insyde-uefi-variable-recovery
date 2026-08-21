# Lenovo QSCN33WW `VariableRuntimeDxe` bug

This has nothing to do with the Linux kernel. The bug is in Insyde's
`VariableRuntimeDxe`, firmware file GUID
`CBD2E4D5-7068-4FF5-B462-9822B4AD8D60`. At runtime, it checks the first live
record in the main UEFI variable store. If that record is not available at
runtime, it gives up on the entire main store and jumps to a fallback store.
What it should do is skip that record and continue walking the main store.

The funny part is that the correct loop already exists a little further down
in the same function. The initial check simply bypasses it.

## Firmware image

The affected machine runs Lenovo BIOS `QSCN33WW`. I extracted
`VariableRuntimeDxe` from both the live BIOS and Lenovo's `KLS71_sign.rom`
update image, then checked that the two copies matched.

The executable is a PE32+ x86-64 UEFI driver with this SHA-256:

```text
fc3369ebc7a0aec50f69114ec51cb63ca91c52b1b9580315761a408a5e33a867
```

The packaged BIOS region has this SHA-256:

```text
804ed0b6b206f1540e0ea189a604e95f14513ac29f665330a747926815844d98
```

I used UEFIExtract to locate the firmware file by GUID and extract the PE32+
image. From there, I used Ghidra and GNU `objdump` to follow the runtime
enumeration path. The UEFI and Linux probes gave me the actual record order
and runtime behaviour, which I then used to check that the disassembly made
sense.

There are no source symbols in the executable, so the offsets below refer to
this exact build.

## Initial `GetNextVariableName()` path

The `GetNextVariableName()` implementation starts at image offset `0x4cc4`.
The first main-store check looks like this:

```text
0x4db4  call  0x2ff8             find the named record, or the first record
0x4dc6  call  0x7860             test whether runtime mode is active
0x4dcd  je    0x4e64             boot-time path: use the main store
0x4dd3  testb $0x4,0x4(%rbx)     test EFI_VARIABLE_RUNTIME_ACCESS
0x4dd7  jne   0x4e64             runtime-accessible: use the main store
0x4ddd  mov   %r12,-0x21(%rbp)   otherwise clear the main-store pointer
0x4dea  call  0xe220             call the fallback enumerator
```

Translated into something vaguely C-like, it is doing this:

```c
status = find_record(name, guid, &record);

if (!EFI_ERROR(status) &&
    runtime_mode() &&
    !(record->Attributes & EFI_VARIABLE_RUNTIME_ACCESS)) {
    record = NULL;
}

if (record == NULL)
    return enumerate_fallback_store(name, guid);

return walk_main_store(record);
```

This only works if the first record happens to be available at runtime. If
the first record is boot-service-only, the code clears the record pointer and
switches stores. It never asks for the next record in the main store.

## Runtime filtering loop

A little later, the same function handles runtime filtering properly:

```text
0x50e5  call  0x7860             test whether runtime mode is active
0x50ee  testb $0x4,0x4(%rbx)     test EFI_VARIABLE_RUNTIME_ACCESS
0x50f2  je    0x511a             if absent, advance to the next record
0x511a  call  0x1030c            get the next record
0x5129  jmp   0x4ef4             continue the loop
```

That part is effectively doing this:

```c
if (runtime_mode() &&
    !(record->Attributes & EFI_VARIABLE_RUNTIME_ACCESS)) {
    record = next_record(record);
    continue;
}
```

So there is no missing variable-store logic to invent. The initial path just
needs to enter this loop even when the first record lacks
`EFI_VARIABLE_RUNTIME_ACCESS`. The loop will skip the record and continue
looking, which is exactly what it already does for every later record.

## Variable order

After deleting the Secure Boot key records, the first live records in the
main store were:

```text
AbtSetup      attributes 0x03: non-volatile, boot-service access
AbtState      attributes 0x03: non-volatile, boot-service access
PlatformLang  attributes 0x07: non-volatile, boot-service and runtime access
```

An empty-name enumeration starts at `AbtSetup`. Since `AbtSetup` does not
have `EFI_VARIABLE_RUNTIME_ACCESS`, the bad branch clears the main-store
pointer and calls the fallback enumerator. It never reaches `PlatformLang`.

If I seed the enumeration with `PlatformLang` instead, it enters the later
loop. The same firmware then continues through the main store and returns the
runtime variables that were supposedly missing.

## UEFI and Linux results

To rule out Linux, I removed it from the test completely. I wrote a UEFI
application that called the same enumeration service before and after
`ExitBootServices()`, without booting an operating system between the two
calls:

```text
before ExitBootServices(): 168 variables
after ExitBootServices():   15 variables
```

Exact `GetVariable()` calls could still read supposedly missing variables
such as `SetupMode` and `BootOrder`. The variables were there. It was the
enumeration that was broken.

I then used a focused Linux probe to start `GetNextVariableName()` from two
different points:

```text
empty start:        15 names, first CrashLogVariable
PlatformLang seed: 136 names, first certdb
```

The exact count changed in later captures as variables were added and
deleted, but the behaviour did not: an empty-name walk returned the tiny
fallback list, while a walk seeded with `PlatformLang` returned the much
larger main-store list.

## Firmware fix

The proper fix is to remove the bad initial runtime filter and let the normal
loop do its job:

```c
status = find_record(name, guid, &record);

if (EFI_ERROR(status))
    return enumerate_fallback_store(name, guid);

return walk_main_store(record);
```

For anyone checking this exact binary, the branch at image offset `0x4dd7`
is:

```text
original: 0F 85 87 00 00 00    JNE 0x4e64
candidate: 90 E9 87 00 00 00   NOP; JMP 0x4e64
```

The byte change is there to show the exact control-flow fix. It is not a
flashable BIOS image.

## Flashing a modified BIOS

Changing the instruction is the easy part, but that does not give me a BIOS I
can safely flash. `VariableRuntimeDxe` is buried inside a compressed firmware
volume which starts at BIOS-region offset
`0x0039b000` and has a size of `0x00460000`. The Insyde Flash Device Map also
marks this exact range as protected:

```text
Address: FF39B000h
Size:    460000h
Hash:    e5f4ff6eaf8d5eb17a8cfdee6d763ce6e5813eac01cf87b361d8c31354ee8ae7
```

So after changing the code, I would still need to rebuild and compress the
firmware volume, update its checksums and protected-range data, and somehow
pass Lenovo's signing and update checks. Without all of that, I do not have a
BIOS update. I have a very expensive way to brick a laptop.

Lenovo and Insyde have the source code and signing keys. They should fix the
branch and ship a properly signed BIOS update.
