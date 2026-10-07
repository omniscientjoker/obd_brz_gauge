# Example Boost + Oil Pressure Theme

This is an example theme for the OBD gauge theme partition system.

## Contents

- `theme_manifest.json` - Theme metadata, colors, and page configuration
- `layout.json` - Custom page layout (boost arc + oil pressure bar)
- `assets/` - Optional theme assets (dial.png and needle.png)

## Assets (Optional)

To add custom artwork:

1. Create `assets/dial.png` - 360x360 RGB background image
2. Create `assets/needle.png` - PNG needle artwork with alpha (optional)

If assets are not provided, the theme will use colored backgrounds.

## Building

```bash
python3 tools/theme_packer/pack_theme.py themes/example_boost_oil firmware/theme_boost_oil.bin
```

This will generate a 4MB binary that can be:
- Flashed directly to the theme_0 partition (offset 0x620000, the only theme slot)
- Or sent via BLE OTA to the device

## Installing

### Via esptool (USB):
```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x620000 firmware/theme_boost_oil.bin
```

### Via BLE OTA:
Use the companion Android app to send the theme binary to the device.

## Theme Features

- Custom boost gauge with arc (range: -0.70 to 2.50 bar)
- Oil pressure bar with 10 segments (0-100 PSI)
- Orange/yellow color scheme
- Preserves all system pages (settings/OTA/bluetooth)
