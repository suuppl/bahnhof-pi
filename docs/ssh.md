# `.ssh/` — Pi SSH Credentials

SSH key pair for passwordless access to the Raspberry Pi. Used by `Makefile` during deployment.

| File | Purpose |
|------|---------|
| `pi_key` | Ed25519 private key — keep secret, never commit |
| `pi_key.pub` | Corresponding public key — installed in `~/.ssh/authorized_keys` on the Pi |
| `config` | SSH client config (hostname alias `pi`) |

## `config`

```
Host pi
  HostName raspberrypi.local
  User dietpi
  IdentityFile /path/to/repo/.ssh/pi_key
  StrictHostKeyChecking no
```

> **Note**: The `Makefile` uses explicit `-i .ssh/pi_key -o StrictHostKeyChecking=no` flags rather than `-F .ssh/config` because the config-file path form proved unreliable in this setup.

## Adding the key to the Pi

```sh
ssh-copy-id -i .ssh/pi_key.pub <user>@raspberrypi.local
# or manually:
cat .ssh/pi_key.pub | ssh <user>@raspberrypi.local "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys"
```

## Security note

`.ssh/pi_key` is a plaintext private key scoped to the Pi on the local network. Do not push this directory to a remote repository.
