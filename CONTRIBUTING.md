# Reporting results

If another system appears to have the same bug, please include:

- the computer model and BIOS version;
- the empty-start and seeded enumeration counts;
- the candidate seed name, GUID, and attributes;
- the final status returned by each enumeration; and
- whether restoring the factory Secure Boot keys changes the result.

Do not post a raw SPI or NVRAM dump. It can contain device identifiers,
firmware settings, network configuration, and other private data. Do not post
Secure Boot private keys, module-signing private keys, serial numbers, support
case numbers, email archives, or unredacted screenshots either.

The read-only UEFI probe does not record variable contents, but its log can
still contain vendor-specific variable names. Read the complete log before
sharing it.

State clearly which results you measured and which conclusions you inferred.
Do not claim that a BIOS version contains this bug unless that exact version
was tested or its `VariableRuntimeDxe` code was checked.
