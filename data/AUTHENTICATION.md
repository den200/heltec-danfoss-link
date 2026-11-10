# Danfoss Link authentication recovered from pairing

## Result

The observed pairing value `<network-key>` is a working
128-bit authentication key for the captured Danfoss Link network ffd2bb2d.
Offline cryptographic verification was followed by a successful local-control test
at 00:19:13 on 2026-10-21; see CONTROL-SUCCESS.md.

During setup the CC transmits:

```
91 0002 80 04 <network-key>
```

The head replies with `91 0002 80 05 00`. Meaning of each handshake status is not
fully established, but the transferred value exactly computes later message tags.
This verifies key transport in the recorded setup, not a negotiated Diffie–Hellman exchange.
It does not establish how other CCs choose keys or whether the value is universal.

## Exact tag construction

1. Start the MAC input at the proprietary operation byte (10, 50 or 90), following
   the `91 0002` command-class/manufacturer prefix.
2. Include the next length byte and all body bytes before the four-byte tag.
3. If length is not divisible by eight, append 80, then zeros to the next eight-byte
   boundary. If already divisible by eight, do not append any padding block.
4. CBC-MAC with XTEA, 32 cycles (64 half-round updates), big-endian key/block words,
   and an eight-byte all-zero IV.
5. Take the final four bytes of the final encrypted block, in transmitted order.

Example captured 17 C command to node 16:

```
full RF frame: ffd2bb2d0141041b10910002100a0e0d0000000006a4904fe4b3ab
MAC input:    100a0e0d0000000006a4
padding:      800000000000
tag:          904fe4b3
RF checksum:  ab
```

The radio Home ID, source/destination, radio sequence, 91 0002 prefix, and RF XOR
checksum are outside this MAC input. The application sequence inside the body IS
included. Replay acceptance and sequence-window policy remain untested.

## Verification and reproducibility

- Discovery corpus: `auth-frames.txt`, 59 distinct frames.
- `probe_auth.py`: 48,000 AES/hash rule combinations across six message families,
  no matches. This is a bounded hypothesis test, not exhaustive cryptanalysis.
- `probe_tea.py`: 2,520 TEA/XTEA CBC-MAC rules; XTEA with the raw key and the
  construction above matches all 59 discovery frames once padding is unified.
- Validation source: `auth-validation-raw.jsonl`, 856 original receiver windows from
  the preceding three hours, exported read-only from Pi SQLite.
- `auth-validation-results.json`: 113 unique frames / 123 occurrences; all pass.
  54 full frames are held out from discovery, including 38 distinct application
  messages absent from discovery. Covers 101 op=10, seven op=50, five op=90 frames.
- `verify_auth_reference.c`: independent implementation with uint32 arithmetic,
  compiled with warnings enabled. 113 matches, zero failures.
- Python XTEA zero-key/zero-block known-answer vector passes (`dee9d4d8f7131ed9`).
- All 113 frames fail with zero key. All 113 fail after an application-bit mutation
  with the outer XOR checksum repaired. Thus acceptance is not merely RF checksum validation.

Reproduce:

```
python3 tools/validate_auth.py data/auth-validation-raw.jsonl data/auth-frames.txt  # key from Keychain
cc -Wall -Wextra -O2 tools/verify_auth_reference.c -o /tmp/danfoss-auth-reference
/tmp/danfoss-auth-reference <network-key> < data/auth-validated-frames.txt
```

## Consequences and next step

The key and tag computation are no longer the blocker. Earlier AES-only failures
were explained by the wrong cipher, not evidence of an unknown hidden nonce.
Identical application messages/tags under different node IDs now have a direct explanation.

For actual local control, implement the transmit framing, application/radio sequence
handling, wake-up/response timing and acknowledgement handling. Then validate one modest
setpoint command on the spare head against the CC's known-good behavior, with a return
path to its original setting. Do not equate an RF ACK alone with acceptance of the setpoint.
Subsequently, the timed responder successfully set the spare head to 28 C with the CC
unplugged; user confirmed the display. See CONTROL-SUCCESS.md for evidence and limitations.
