# Failure mechanism

## Normal UEFI enumeration

Linux calls the firmware service `GetNextVariableName()` with an empty name to
start variable enumeration. The firmware must skip variables that do not have
`EFI_VARIABLE_RUNTIME_ACCESS` after `ExitBootServices()`. It must then return
the first variable that does have `EFI_VARIABLE_RUNTIME_ACCESS`.

`ExitBootServices()` ends the boot-service phase and starts the operating-
system runtime phase. After that point, firmware must hide boot-only variables
and keep walking until it finds a runtime variable.

## Broken Insyde enumeration

Here is where Insyde gets it wrong. `VariableRuntimeDxe` checks only the first
live record in the main variable store. If that record lacks
`EFI_VARIABLE_RUNTIME_ACCESS`, the code throws away the main-store search and
calls a fallback-store enumerator.

It should just try the next record. It does not.

This is why the result depends on the history of the variable store:

1. Factory Secure Boot keys occupy the first physical records. Those records
   have runtime access. Enumeration works.
2. Firmware deletes `PK`, `KEK`, `db`, and `dbx` to enter Setup Mode.
3. A boot-service-only record can become the first live record.
4. Runtime enumeration then abandons the main store.
5. Linux creates `efivarfs` entries only for the small fallback list.

Exact `GetVariable()` calls still find hidden variables. The main store remains
present and readable.

## Seeded enumeration

`GetNextVariableName()` also accepts the previous variable name and GUID. When
the caller starts from a known runtime variable inside the main store, the
driver enters its later record loop. That later loop correctly skips records
without runtime access.

The workaround does this:

1. Count the normal empty-name list.
2. Confirm the seed with exact `GetVariable()`.
3. Require the seed to have `EFI_VARIABLE_RUNTIME_ACCESS` unless specified
otherwise with the `skip_non_runtime=1` flag.
4. Walk from that seed with `GetNextVariableName()`.
5. Require a larger list and a normal `EFI_NOT_FOUND` end.
6. In `expose=1` mode, create only missing in-memory `efivarfs` entries.

The module does not call `SetVariable()` during recovery.

## NVRAM parser limits

The NVRAM parser reads physical record order, but firmware can merge several
stores, volatile variables, or vendor-specific formats into one logical list.
The physical order is useful evidence, not proof.

The module treats the parser result as a candidate. It still checks the seed
with exact `GetVariable()` and a seeded firmware walk. If Linux cannot read a
live SPI device, use a known seed such as `PlatformLang` or run the UEFI probe.

The full Lenovo disassembly, instruction offsets, and source-level repair are
in [Lenovo QSCN33WW `VariableRuntimeDxe` analysis](lenovo-bios-analysis.md).
