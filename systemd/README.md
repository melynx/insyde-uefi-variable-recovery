# systemd one-shot service

The systemd one-shot service runs the Linux recovery once during each boot.
It waits until local filesystems are available. It does not leave the kernel
module loaded.

The helper performs these steps:

1. Require a signed module built for the exact running kernel version.
2. Require `/sys/firmware/efi/efivars` to be an `efivarfs` mount.
3. Preserve whether that mount started read-only.
4. Temporarily remount it read-write when required.
5. Load `insyde_efivarfs_recovery` with `expose=1`.
6. Unload the module.
7. Restore the original read-only mount state, including after a failure.

The module's own safety checks still apply. When normal firmware enumeration
works, such as after restoring factory Secure Boot keys, the module reports
that the collapse signature is absent and creates nothing.

## Install

Build and sign `kernel/insyde_efivarfs_recovery.ko` first. Install the signed
module under the exact output of `uname -r`:

```sh
kernel_version=$(uname -r)
sudo install -D -m 0644 kernel/insyde_efivarfs_recovery.ko \
  "/usr/local/lib/insyde-efivarfs-recovery/$kernel_version/insyde_efivarfs_recovery.ko"
sudo install -D -m 0755 systemd/insyde-efivarfs-recovery-once \
  /usr/local/libexec/insyde-efivarfs-recovery-once
sudo install -D -m 0644 systemd/insyde-efivarfs-recovery.service \
  /etc/systemd/system/insyde-efivarfs-recovery.service
sudo systemctl daemon-reload
sudo systemctl enable --now insyde-efivarfs-recovery.service
```

Review the result:

```sh
systemctl status insyde-efivarfs-recovery.service
journalctl -u insyde-efivarfs-recovery.service -b
findmnt -no FSTYPE,OPTIONS /sys/firmware/efi/efivars
```

Install a newly built and signed module under the new kernel version after
each kernel update. If the matching file is absent, the service fails without
loading a module and restores the original mount state.

## Measured result

On the original Lenovo test machine, the enabled service recovered 158 entries
from an empty-start firmware list of 15. It reported zero failures, unloaded
the module, and restored the read-only `efivarfs` mount. A second run found all
158 entries already present and also completed with zero failures.
