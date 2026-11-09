#pragma once

#include <GfxRenderer.h>
#include <qrcode.h>

#include <memory>
#include <string>

#include "components/themes/BaseTheme.h"

namespace QrUtils {

// A finished code. Encoding is slow, so screens that repaint keep one of these.
struct Code {
  std::unique_ptr<uint8_t[]> modules;
  QRCode qrcode{};
};

// False when the text cannot be encoded or memory is short.
bool build(const std::string& textPayload, Code& out);
void draw(const GfxRenderer& renderer, const Rect& bounds, Code& code);

// Renders a QR code with the given text payload within the specified bounding box.
void drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload);

}  // namespace QrUtils
