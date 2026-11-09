#include "QrUtils.h"

#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <memory>

#include "Logging.h"

bool QrUtils::build(const std::string& textPayload, Code& out) {
  // Byte-mode ECC_LOW capacities. The encoder does not check them, so a longer text would overrun its
  // stack buffers. Version 30 is the limit: its encoder already takes about 7 KB of stack.
  struct Capacity {
    int version;
    size_t bytes;
  };
  static constexpr Capacity CAPACITIES[] = {{4, 78}, {10, 271}, {20, 858}, {30, 1732}};
  static constexpr size_t MAX_QR_CAPACITY = 1732;

  size_t len = textPayload.length();
  std::string truncated;
  const char* payload = textPayload.c_str();
  if (len > MAX_QR_CAPACITY) {
    len = utf8SafeTruncateBuffer(textPayload.c_str(), static_cast<int>(MAX_QR_CAPACITY));
    truncated = textPayload.substr(0, len);
    payload = truncated.c_str();
  }

  int version = 30;
  for (const Capacity& capacity : CAPACITIES) {
    if (len <= capacity.bytes) {
      version = capacity.version;
      break;
    }
  }

  out.modules = makeUniqueNoThrow<uint8_t[]>(qrcode_getBufferSize(version));
  if (!out.modules) {
    LOG_ERR("QR", "OOM: QR buffer");
    return false;
  }
  // ECC_LOW for max capacity.
  if (qrcode_initText(&out.qrcode, out.modules.get(), version, ECC_LOW, payload) != 0) {
    LOG_ERR("QR", "Text too long for QR code");
    out.modules.reset();
    return false;
  }
  return true;
}

void QrUtils::draw(const GfxRenderer& renderer, const Rect& bounds, Code& code) {
  if (!code.modules) return;
  const int maxDim = std::min(bounds.width, bounds.height);
  const int px = std::max(maxDim / code.qrcode.size, 1);
  const int qrDisplaySize = code.qrcode.size * px;
  const int xOff = bounds.x + (bounds.width - qrDisplaySize) / 2;
  const int yOff = bounds.y + (bounds.height - qrDisplaySize) / 2;
  for (uint8_t cy = 0; cy < code.qrcode.size; cy++) {
    for (uint8_t cx = 0; cx < code.qrcode.size; cx++) {
      if (qrcode_getModule(&code.qrcode, cx, cy)) renderer.fillRect(xOff + px * cx, yOff + px * cy, px, px, true);
    }
  }
}

void QrUtils::drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload) {
  Code code;
  if (build(textPayload, code)) draw(renderer, bounds, code);
}
