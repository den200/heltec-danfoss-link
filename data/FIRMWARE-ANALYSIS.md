# CC firmware inspection — 21 October 2026

Read-only inspection; originals unchanged. Evidence: firmware-key-audit.json.

- All three files match manifest sizes and MD5 values; SHA-256 recorded in evidence.
- ccimage.bin is a structurally consistent NXP/Freescale SB 1.1 image, 48,585,216
  bytes, one section and one key-dictionary entry. Header SHA-1 and declared image
  length pass. Header/section table total 112 bytes; key dictionary starts at112.
- Tested the captured 16-byte radio authentication key as the AES-128 SB wrapping
  key by computing the dictionary header CBC-MAC, IV zero, over those112bytes.
  It does not match. Zero key also does not match. This rules out direct reuse as
  the image's wrapping key, not all possible derivations or data-key hypotheses.
  No image decryption or device OTP extraction was performed.
- zensys_firmware.hex: all2047 Intel HEX checksums pass;32678 populated bytes,
  address0..32725. Readable version Z-Wave3.52. This is the radio-side firmware;
  no evidence yet that it contains Danfoss room/configuration logic.
- extern_eep.hex: all630 record checksums pass;9841 populated bytes. ZeNsYs marker.
  Neither decoded HEX file contains our current radio key verbatim. Generic update
  contents are not a dump of this household's running CC or its current network.

Format references (primary implementation):
https://raw.githubusercontent.com/Rockbox/rockbox/master/utils/imxtools/sbtools/sb.h
https://raw.githubusercontent.com/Rockbox/rockbox/master/utils/imxtools/sbtools/sb.c

Earlier NOTES.md identifies the boot key as OTP-backed. The local file alone does
not establish whether the provisioned key is unique to one CC or shared by a
product family. Do not conflate it with the household key distributed at inclusion.

Pragmatic path: capture genuine CC configuration to map/preserve its fields.
Firmware decryption is not presently an available shortcut. Radio-side HEX remains
available for targeted Serial API investigation if captures leave a specific gap.
