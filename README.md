Enable an autoscroll mechanism for your mouse, similar to middle-click in web browsers. 

- Hold right-click and move to start autoscrolling, release to stop.
- Press left-click during autoscrolling to go all the way to the top or bottom.
- Press and release right-click without moving to do a regular right-click.

# TODO

- Choose a mouse button
- Horizontal scrolling
- Interpret mouse events using acceleration
- Options: sensitivity, braking, deadzone, acceleration

# Compile

Requirements: install `libevdev`, `libinput` and `gcc` using your package manager

```sh
make build
```

# Usage 

Identify the device path of the mouse you want to remap: `ls -l /dev/input/by-id/` or `evtest`

```sh
mouse-autoscroll /dev/input/...
```

# Install

Configure the command arguments in `mouse-autoscroll.destkop` as described above.

```sh
cp mouse-autoscroll.destkop ~/.config/autostart/
```

TODO: use systemd