"""Native captured-frame tests. No ESPHome, BLE radio or hardware required.

Run: python3 test_protocol.py [--sdk /path/to/MacOSX.sdk]
Only a temporary executable is created, and removed on completion.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile

SOURCE = r'''
#include "charger_protocol.h"
#include <cassert>
#include <cstdio>
#include <vector>
namespace protocol = esphome::lxy_charger::protocol;

int main() {
  uint8_t out[protocol::MaxFrameSize];
  struct Capture { uint16_t v, a; std::vector<uint8_t> bytes; };
  const Capture captured[] = {
    {584,50,{0x5e,0x5e,0x07,0x03,0x01,0x02,0x48,0x00,0x32,0x7d}},
    {584,49,{0x5e,0x5e,0x07,0x03,0x01,0x02,0x48,0x00,0x31,0x7e}},
    {584,51,{0x5e,0x5e,0x07,0x03,0x01,0x02,0x48,0x00,0x33,0x7c}},
    {583,51,{0x5e,0x5e,0x07,0x03,0x01,0x02,0x47,0x00,0x33,0x73}},
    {582,51,{0x5e,0x5e,0x07,0x03,0x01,0x02,0x46,0x00,0x33,0x72}},
  };
  for (const auto &capture : captured) {
    const size_t size = protocol::encodeSet(capture.v, capture.a, out);
    assert(std::vector<uint8_t>(out, out + size) == capture.bytes);
  }
  const uint8_t key = 1;
  const std::vector<uint8_t> query{0x5e,0x5e,0x03,0x02,0x01,0x00};
  const std::vector<uint8_t> status{0x5e,0x5e,0x02,0x04,0x06};
  auto size = protocol::encode(2, &key, 1, out);
  assert(std::vector<uint8_t>(out, out + size) == query);
  size = protocol::encode(4, nullptr, 0, out);
  assert(std::vector<uint8_t>(out, out + size) == status);
  const std::vector<uint8_t> reply{0x5e,0x5e,0x07,0x82,0x01,0x02,0x48,0x00,0x33,0xfd};
  const std::vector<uint8_t> echo{0x5e,0x5e,0x07,0x83,0x01,0x02,0x47,0x00,0x33,0xf3};
  assert(protocol::readU16(reply.data() + 5) == 584);
  assert(protocol::readU16(reply.data() + 7) == 51);
  std::vector<std::vector<uint8_t>> received;
  auto collect = [&](const uint8_t *frame, size_t length) { received.emplace_back(frame, frame + length); };
  for (size_t split = 0; split <= reply.size(); ++split) {
    protocol::Decoder decoder;
    received.clear();
    decoder.feed(reply.data(), split, collect);
    decoder.feed(reply.data() + split, reply.size() - split, collect);
    assert(received == std::vector<std::vector<uint8_t>>{reply});
  }
  protocol::Decoder decoder;
  auto combined = reply;
  combined.insert(combined.end(), echo.begin(), echo.end());
  received.clear();
  decoder.feed(combined.data(), combined.size(), collect);
  assert((received == std::vector<std::vector<uint8_t>>{reply, echo}));
  auto corrupt = reply;
  corrupt.back() ^= 1;
  corrupt.insert(corrupt.end(), echo.begin(), echo.end());
  const uint8_t noise[]{0xaa,0x5e,0x00,0x5e,0x5e,0x01};
  decoder.reset(); received.clear();
  decoder.feed(noise, sizeof(noise), collect);
  decoder.feed(corrupt.data(), corrupt.size(), collect);
  assert(received == std::vector<std::vector<uint8_t>>{echo});
  assert(decoder.checksumErrors == 1);
  // A callback may invalidate the link and reset the decoder. Check no
  // underflow or invalid memmove occurs when control returns to feed().
  decoder.reset(); received.clear();
  decoder.feed(combined.data(), combined.size(), [&](const uint8_t *frame, size_t length) {
    collect(frame, length);
    decoder.reset();
  });
  assert((received == std::vector<std::vector<uint8_t>>{reply, echo}));
  puts("PASS: captured encodings, config decoding, all splits, coalescing, noise/checksum recovery, callback reset");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", help="Explicit matching macOS SDK when needed")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="lxy-protocol-") as temp:
        executable = str(Path(temp) / "protocol_test")
        command = ["c++", "-std=c++17", "-Wall", "-Wextra", "-pedantic", "-I", str(Path(__file__).parent)]
        if args.sdk:
            command += ["-isysroot", args.sdk]
        command += ["-x", "c++", "-", "-o", executable]
        subprocess.run(command, input=SOURCE, text=True, check=True)
        subprocess.run([executable], check=True)


if __name__ == "__main__":
    main()
