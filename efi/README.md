# UEFI variable-order probe

This GNU-EFI application asks the firmware to enumerate its variables. It
records:

- firmware and UEFI revisions;
- `QueryVariableInfo()` results;
- every name returned by `GetNextVariableName()`;
- the first enumerated name with `EFI_VARIABLE_RUNTIME_ACCESS`;
- exact `GetVariable()` status, attributes, and size for each returned name;
- exact probes for Secure Boot state, key databases, and boot-order variables;
- the result with a 512-byte name buffer and a 65,536-byte name buffer.

The application never calls `SetVariable()`. It never records variable
contents. It replaces `\uefi-var-probe.log` on the same filesystem from which
it starts.

## Build

Install GNU-EFI headers and libraries. Then run:

```sh
make
```

The Makefile defaults match a common x86-64 layout. Override
`GNU_EFI_LIBDIR`, `GNU_EFI_CRT`, or `GNU_EFI_LDS` when the distribution uses a
different path.

Copy `build/uefi-var-probe.efi` to a FAT USB drive. Start it from the firmware
boot menu or a UEFI shell. Press a key after it finishes, then copy
`uefi-var-probe.log` from the USB drive.
