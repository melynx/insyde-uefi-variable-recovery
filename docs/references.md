# Technical references

- [UEFI Specification 2.11: Runtime Services](https://uefi.org/specs/UEFI/2.11/08_Services_Runtime_Services.html)
- [Linux kernel documentation: efivarfs](https://docs.kernel.org/filesystems/efivarfs.html)
- [`sbctl` issue #407: missing `SetupMode` variable](https://github.com/Foxboron/sbctl/issues/407)
- [Framework issue #30: Can't enter Secure Boot setup mode](https://github.com/FrameworkComputer/SoftwareFirmwareIssueTracker/issues/30)
- [Framework issue #76: EFI variable reads fail after BIOS 3.08 when dbx is empty or altered](https://github.com/FrameworkComputer/SoftwareFirmwareIssueTracker/issues/76)
- [Framework Laptop 13 13th Gen Intel Core BIOS 3.08 release and checksums](https://community.frame.work/t/framework-laptop-13-13th-gen-intel-core-bios-3-08-release-stable/70435)
- [Framework forum: Secure Boot Setup Mode failure reported in August 2023](https://community.frame.work/t/secureboot-setup-mode/14889/18)
- [Framework forum: Laptop 13 Ryzen 7040 BIOS 3.20 `STABLE` release thread](https://community.frame.work/t/framework-laptop-13-ryzen-7040-bios-3-20-release-stable/83283)
- [Ars Technica: Framework responds after BIOS 3.20 bricks Ryzen 7040 laptops](https://arstechnica.com/gadgets/2026/08/framework-responds-to-complaints-that-bios-update-bricked-ryzen-7040-laptops/)
- [TianoCore EDK II VariableFormat.h](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Include/Guid/VariableFormat.h)
- [TianoCore EDK II AuthenticatedVariableFormat.h](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Include/Guid/AuthenticatedVariableFormat.h)

The UEFI specification defines what `GetVariable()`, `GetNextVariableName()`,
and `ExitBootServices()` are supposed to do. The Linux documentation explains
how those variables appear through `efivarfs`.

`VariableFormat.h` defines the standard variable-store GUID, the 28-byte store
header, the `0x55aa` record start marker, and the `0x3f` added state used by
`tools/nvram_records.py`. The authenticated-store GUID and header layout come
from EDK II's `AuthenticatedVariableFormat.h`.

I made the parser test both record layouts even when the store GUID declares
one layout, because the measured Insyde store used authenticated record
headers below the standard store GUID.
