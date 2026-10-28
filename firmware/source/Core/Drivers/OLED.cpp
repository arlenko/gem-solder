/*
 * OLED.cpp
 *
 *  Created on: 29Aug.,2017
 *      Author: Ben V. Brown
 */

#include "Buttons.hpp"
#include "Settings.h"
#include "Translation.h"
#include "cmsis_os.h"
#include "configuration.h"
#include "power.hpp"
#include <OLED.hpp>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// rendering to the buffer
uint8_t *OLED::stripPointers[OLED_PAGES]; // Pointers to the strips to allow for buffer having extra content
uint8_t *OLED::framebufferBase;
uint8_t  OLED::drawPageCount = OLED_PAGES;

bool OLED::inLeftHandedMode; // Whether the screen is in left or not (used for
// offsets in GRAM)
OLED::DisplayState OLED::displayState;
int16_t            OLED::cursor_x, OLED::cursor_y;
bool               OLED::initDone = false;
uint8_t            OLED::displayOffset;
alignas(uint32_t) uint8_t OLED::screenBuffer[16 + (OLED_WIDTH * OLED_PAGES) + 10]; // The data buffer
alignas(uint32_t) uint8_t OLED::secondFrameBuffer[16 + (OLED_WIDTH * OLED_PAGES) + 10];
uint32_t OLED::displayChecksum;
/*
 * Setup params for the OLED screen
 * http://www.displayfuture.com/Display/datasheet/controller/SSD1307.pdf
 * All commands are prefixed with 0x80
 * Data packets are prefixed with 0x40
 */
I2C_CLASS::I2C_REG OLED_Setup_Array[] = {
    /**/
    {0x80,         OLED_OFF, 0}, /* Display off */
    {0x80,     OLED_DIVIDER, 0}, /* Set display clock divide ratio / osc freq */
    {0x80,             0x80, 0}, /* Divide ratios */
    {0x80,             0xA8, 0}, /* Set Multiplex Ratio */
    {0x80,  OLED_HEIGHT - 1, 0}, /* Multiplex ratio adjusts how far down the matrix it scans */
    {0x80,             0xC0, 0}, /* Set COM Scan direction */
    {0x80,             0xD3, 0}, /* Set vertical Display offset */
    {0x80,             0x00, 0}, /* 0 Offset */
    {0x80,             0x40, 0}, /* Set Display start line to 0 */
#ifdef OLED_SEGMENT_MAP_REVERSED
    {0x80,             0xA1, 0}, /* Set Segment remap to normal */
#else
    {0x80, 0xA0, 0}, /* Set Segment remap to normal */
#endif
    {0x80,             0x8D, 0}, /* Charge Pump */
    {0x80,             0x14, 0}, /* Charge Pump settings */
    {0x80,             0xDA, 0}, /* Set VCOM Pins hardware config */
    {0x80, OLED_VCOM_LAYOUT, 0}, /* Combination 0x2 or 0x12 depending on OLED model */
    {0x80,             0x81, 0}, /* Brightness */
    {0x80,             0x00, 0}, /* ^0 */
    {0x80,             0xD9, 0}, /* Set pre-charge period */
    {0x80,             0xF1, 0}, /* Pre charge period */
    {0x80,             0xDB, 0}, /* Adjust VCOMH regulator ouput */
    {0x80,             0x30, 0}, /* VCOM level */
    {0x80,             0xA4, 0}, /* Enable the display GDDR */
    {0x80,             0xA6, 0}, /* Normal display */
    {0x80,             0x20, 0}, /* Memory Mode */
    {0x80,             0x00, 0}, /* Wrap memory */
    {0x80,          OLED_ON, 0}, /* Display on */
};
// Setup based on the SSD1307 and modified for the SSD1306

const uint8_t REFRESH_COMMANDS[17] = {
    // Set display ON:
    0x80,
    0xAF, // cmd

    // Set column address:
    //  A[6:0] - Column start address = 0x20
    //  B[6:0] - Column end address = 0x7F
    0x80,
    0x21, // cmd
    0x80,
    OLED_GRAM_START, // A
    0x80,
    OLED_GRAM_END, // B

    // Set COM output scan direction (normal mode, COM0 to COM[N-1])
    0x80,
    0xC0,

    // Set page address:
    //  A[2:0] - Page start address = 0
    //  B[2:0] - Page end address = 1
    0x80,
    0x22, // cmd
    0x80,
    0x00, // A
    0x80,
    OLED_PAGES - 1, // B

    // Start of data
    0x40,
};

/*
 * Animation timing function that follows a bezier curve.
 * @param t A given percentage value [0..<100]
 * Returns a new percentage value with ease in and ease out.
 * Original floating point formula: t * t * (3.0f - 2.0f * t);
 */
static uint16_t easeInOutTiming(uint16_t t) { return t * t * (300 - 2 * t) / 10000; }

/*
 * Returns the value between a and b, using a percentage value t.
 * @param a The value associated with 0%
 * @param b The value associated with 100%
 * @param t The percentage [0..<100]
 */
static uint16_t lerp(uint16_t a, uint16_t b, uint16_t t) { return a + t * (b - a) / 100; }

void OLED::initialize() {
  cursor_x = cursor_y = 0;
  inLeftHandedMode    = false;

  setFramebuffer(screenBuffer);

  displayOffset = 0;
  memcpy(&screenBuffer[0], &REFRESH_COMMANDS[0], sizeof(REFRESH_COMMANDS));
  memcpy(&secondFrameBuffer[0], &REFRESH_COMMANDS[0], sizeof(REFRESH_COMMANDS));

  // Set the display to be ON once the settings block is sent and send the
  // initialisation data to the OLED.

  for (int tries = 0; tries < 10; tries++) {
    if (I2C_CLASS::writeRegistersBulk(DEVICEADDR_OLED, OLED_Setup_Array,
                                      sizeof(OLED_Setup_Array) / sizeof(OLED_Setup_Array[0]))) {
      tries = 11;
    }
  }
  setDisplayState(DisplayState::ON);
  initDone = true;
}

void OLED::setFramebuffer(uint8_t *buffer) {
  framebufferBase = buffer;
  resetDrawingWindow();
}

void OLED::setDrawingWindow(uint8_t firstPage, uint8_t pageCount) {
  if (pageCount == 0 || firstPage + pageCount > OLED_PAGES) {
    // Fall back to the whole framebuffer on invalid windows
    firstPage = 0;
    pageCount = OLED_PAGES;
  }
  drawPageCount = pageCount;
  for (uint8_t page = 0; page < pageCount; page++) {
    stripPointers[page] = &framebufferBase[FRAMEBUFFER_START + (OLED_WIDTH * (firstPage + page))];
  }
  // Keep the unused slots pointing at a valid strip in case a caller forgets the window
  for (uint8_t page = pageCount; page < OLED_PAGES; page++) {
    stripPointers[page] = stripPointers[pageCount - 1];
  }
}

void OLED::clearScreen() {
  resetDrawingWindow();
  memset(framebufferBase + FRAMEBUFFER_START, 0, OLED_WIDTH * OLED_PAGES);
}

/*
 * Prints a char to the screen.
 * UTF font handling is done using the two input chars.
 * Precursor is the command char that is used to select the table.
 */
void OLED::drawChar(const uint16_t charCode, const FontStyle fontStyle, const uint8_t soft_x_limit) {
  const uint8_t *currentFont;
  static uint8_t fontWidth, fontHeight;
  uint16_t       index;
  switch (fontStyle) {
  case FontStyle::EXTRAS:
    currentFont = ExtraFontChars;
    index       = charCode;
    fontHeight  = 16;
    fontWidth   = 12;
    break;
  case FontStyle::SMALL:
  case FontStyle::LARGE:
  default:
    currentFont = nullptr;
    index       = 0;
    switch (fontStyle) {
    case FontStyle::SMALL:
      // 128x32 panels use the larger Terminus 8x16 small font.
      fontHeight = 16;
      fontWidth  = 8;
      break;
    case FontStyle::LARGE:
    default:
      // 128x32 panels use the larger Terminus 12x24 large font.
      fontHeight = 24;
      fontWidth  = 12;
      break;
    }
    if (charCode == '\x01' && cursor_y == 0) { // 0x01 is used as new line char
      setCursor(soft_x_limit, fontHeight);
      return;
    } else if (charCode <= 0x01) {
      return;
    }

    currentFont = fontStyle == FontStyle::SMALL ? FontSectionInfo.font06_start_ptr : FontSectionInfo.font12_start_ptr;
    index       = charCode - 2;
    break;
  }
  const uint8_t *charPointer = currentFont + ((fontWidth * (fontHeight / 8)) * index);
  const uint8_t  shift       = cursor_y & 7;
  if (shift == 0) {
    drawArea(cursor_x, cursor_y, fontWidth, fontHeight, charPointer);
  } else {
    // y is not strip-aligned: shift each column across strips so a glyph can be
    // vertically centred (e.g. a 24px large readout at y=4 on a 32px panel).
    const uint8_t srcStrips = fontHeight / 8;
    const int16_t baseStrip = cursor_y / 8;
    for (uint8_t col = 0; col < fontWidth; col++) {
      const int16_t x = cursor_x + col;
      if (x < 0 || x >= OLED_WIDTH) {
        continue;
      }
      uint32_t bits = 0;
      for (uint8_t s = 0; s < srcStrips; s++) {
        bits |= (uint32_t)charPointer[(s * fontWidth) + col] << (8 * s);
      }
      bits <<= shift;
      for (uint8_t s = 0; s <= srcStrips; s++) {
        const int16_t destStrip = baseStrip + s;
        if (destStrip < 0 || destStrip >= (int16_t)drawPageCount) {
          continue;
        }
        stripPointers[destStrip][x] = (bits >> (8 * s)) & 0xFF;
      }
    }
  }
  cursor_x += fontWidth;
}

/*
 * Draws a one pixel wide scrolling indicator. y is the upper vertical position
 * of the indicator in pixels (0..<16).
 */
void OLED::drawScrollIndicator(uint8_t y, uint8_t height) {
  const uint16_t endRow = static_cast<uint16_t>(y) + height; // exclusive

  for (uint8_t page = 0; page < drawPageCount; page++) {
    const uint16_t pageTop = static_cast<uint16_t>(page) * 8;
    uint8_t        mask    = 0;
    for (uint8_t bit = 0; bit < 8; bit++) {
      const uint16_t row = pageTop + bit;
      if (row >= y && row < endRow) {
        mask |= static_cast<uint8_t>(1u << bit);
      }
    }
    fillArea(OLED_WIDTH - 1, static_cast<int8_t>(pageTop), 1, 8, mask);
  }
}

/**
 * Masks (removes) the scrolling indicator, i.e. clears the rightmost column
 * on the screen. This operates directly on the OLED graphics RAM, as this
 * is intended to be used before calling `OLED::transitionScrollDown()`.
 */
void OLED::maskScrollIndicatorOnOLED() {
  // The right-most column depends on the screen rotation, so just take
  // it from the screen buffer which is updated by `OLED::setRotation`.
  uint8_t rightmostColumn              = screenBuffer[7];
  uint8_t maskCommands[7 + OLED_PAGES] = {
      // Set column address:
      //  A[6:0] - Column start address = rightmost column
      //  B[6:0] - Column end address = rightmost column
      0x80,
      0x21, // cmd
      0x80,
      rightmostColumn, // A
      0x80,
      rightmostColumn, // B

      // Start of data
      0x40,
      // Remaining bytes are zero-initialised: one per 8px strip
  };
  I2C_CLASS::Transmit(DEVICEADDR_OLED, maskCommands, sizeof(maskCommands));
}

/**
 * Plays a transition animation between two framebuffers.
 * @param forwardNavigation Direction of the navigation animation.
 *
 * If forward is true, this displays a forward navigation to the second framebuffer contents.
 * Otherwise a rewinding navigation animation is shown to the second framebuffer contents.
 */
void OLED::transitionSecondaryFramebuffer(const bool forwardNavigation, const TickType_t viewEnterTime) {
  bool     buttonsReleased = getButtonState() == BUTTON_NONE;
  uint8_t *stripBackPointers[OLED_PAGES];
  for (uint8_t page = 0; page < OLED_PAGES; page++) {
    stripBackPointers[page] = &secondFrameBuffer[FRAMEBUFFER_START + (OLED_WIDTH * page)];
  }

  TickType_t totalDuration = TICKS_100MS * 5; // 500ms
  TickType_t duration      = 0;
  TickType_t start         = xTaskGetTickCount();
  uint8_t    offset        = 0;
  uint32_t   loopCounter   = 0;
  TickType_t startDraw     = xTaskGetTickCount();
  while (duration <= totalDuration) {
    loopCounter++;
    duration          = xTaskGetTickCount() - start;
    uint16_t progress = ((duration * 100) / totalDuration); // Percentage of the period we are through for animation
    progress          = easeInOutTiming(progress);
    progress          = lerp(0, OLED_WIDTH, progress);
    // Constrain
    if (progress > OLED_WIDTH) {
      progress = OLED_WIDTH;
    }

    // When forward, current contents move to the left out.
    // Otherwise the contents move to the right out.
    uint8_t oldStart    = forwardNavigation ? 0 : progress;
    uint8_t oldPrevious = forwardNavigation ? progress - offset : offset;

    // Content from the second framebuffer moves in from the right (forward)
    // or from the left (not forward).
    uint8_t newStart = forwardNavigation ? OLED_WIDTH - progress : 0;
    uint8_t newEnd   = forwardNavigation ? 0 : OLED_WIDTH - progress;

    offset = progress;

    for (uint8_t page = 0; page < OLED_PAGES; page++) {
      memmove(&stripPointers[page][oldStart], &stripPointers[page][oldPrevious], OLED_WIDTH - progress);
      memmove(&stripPointers[page][newStart], &stripBackPointers[page][newEnd], progress);
    }

    if (loopCounter % 2 == 0) {
      refresh();
    }

    vTaskDelayUntil(&startDraw, TICKS_100MS / 7);
    buttonsReleased |= getButtonState() == BUTTON_NONE;
    if (getButtonState() != BUTTON_NONE && buttonsReleased) {
      memcpy(screenBuffer + FRAMEBUFFER_START, secondFrameBuffer + FRAMEBUFFER_START,
             sizeof(screenBuffer) - FRAMEBUFFER_START);
      refresh(); // Now refresh to write out the contents to the new page
      return;
    }
  }
  refresh(); // redraw at the end if required
}

void OLED::useSecondaryFramebuffer(bool useSecondary) {
  if (useSecondary) {
    setFramebuffer(secondFrameBuffer);
  } else {
    setFramebuffer(screenBuffer);
  }
}

/**
 * Scrolls the screen contents by `rows` pixel rows, pulling the matching rows of
 * the secondary framebuffer in from the opposite edge. `rows == OLED_HEIGHT`
 * performs the historical full-screen transition.
 *
 * Sadly we cant use the hardware scroll: the panel has no spare GRAM to hold
 * both screens at once, so we shift one row at a time and redraw.
 *
 * **This function blocks until the transition has completed or user presses button**
 */
void OLED::transitionScroll(const bool down, const uint8_t rows, const TickType_t viewEnterTime) {
  (void)viewEnterTime;
  if (rows == 0) {
    return;
  }
  TickType_t startDraw       = xTaskGetTickCount();
  bool       buttonsReleased = getButtonState() == BUTTON_NONE;

  // For partial scrolls, pre-load the new frame's content that will remain on
  // screen after the slide into the outgoing frame's opposite edge. This makes
  // the incoming content (e.g. the newly highlighted menu row) correct from the
  // first animation step instead of popping in when the transition finishes.
  if (rows < OLED_HEIGHT) {
    const uint8_t keptPages = (OLED_HEIGHT - rows) / 8; // rows are pixel rows
    const uint8_t dstPage   = down ? (rows / 8) : 0;
    const uint8_t srcPage   = down ? 0 : (rows / 8);
    for (uint8_t page = 0; page < keptPages; page++) {
      const uint16_t dst = FRAMEBUFFER_START + (OLED_WIDTH * (dstPage + page));
      const uint16_t src = FRAMEBUFFER_START + (OLED_WIDTH * (srcPage + page));
      // Leave the right-most column alone so the scroll indicator is not disturbed.
      memcpy(&screenBuffer[dst], &secondFrameBuffer[src], OLED_WIDTH - 1);
    }
  }

  // The panel is updated over a blocking software I2C bus, so on the 64px
  // display each refresh moves twice the data and sending every pixel row makes
  // the animation noticeably slower. Animate in coarser frames there, while
  // keeping the 32px display's original timing untouched.
  const uint8_t    pixelsPerFrame = (OLED_HEIGHT > 32) ? 2 : 1;
  const uint8_t    refreshEvery   = (OLED_HEIGHT > 32) ? 1 : 2;
  const TickType_t framePeriod    = (OLED_HEIGHT > 32) ? (TICKS_100MS * 2 / 10) : (TICKS_100MS / 7);
  const uint8_t    frameCount     = (rows + pixelsPerFrame - 1) / pixelsPerFrame;

  uint8_t heightPos = 0;
  for (uint8_t frame = 0; frame < frameCount; frame++) {
    const uint8_t rowsThisFrame = ((rows - heightPos) < pixelsPerFrame) ? (rows - heightPos) : pixelsPerFrame;
    for (uint8_t sub = 0; sub < rowsThisFrame; sub++, heightPos++) {
      // Pixel row of the new frame that enters the vacated edge on this step.
      const uint8_t incomingRow  = down ? (OLED_HEIGHT - rows + heightPos) : (rows - 1 - heightPos);
      const uint8_t incomingPage = incomingRow / 8;
      const uint8_t incomingBit  = incomingRow % 8;

      for (uint8_t xPos = 0; xPos < OLED_WIDTH; xPos++) {
        const uint8_t incomingByte = secondFrameBuffer[FRAMEBUFFER_START + (OLED_WIDTH * incomingPage) + xPos];
        const uint8_t incoming     = (incomingByte >> incomingBit) & 0x01;

        if (down) {
          // Move the MSB of each strip into the LSB of the strip above it
          for (uint8_t page = 0; page < OLED_PAGES - 1; page++) {
            const uint16_t pos     = FRAMEBUFFER_START + (OLED_WIDTH * page) + xPos;
            const uint16_t nextPos = pos + OLED_WIDTH;
            screenBuffer[pos]      = (screenBuffer[pos] >> 1) | ((screenBuffer[nextPos] & 0x01) << 7);
          }
          // The bottom strip takes the next row of the new frame
          const uint16_t lastPos = FRAMEBUFFER_START + (OLED_WIDTH * (OLED_PAGES - 1)) + xPos;
          screenBuffer[lastPos]  = (screenBuffer[lastPos] >> 1) | (incoming << 7);
        } else {
          // Shift every strip into the strip below it
          for (uint8_t page = OLED_PAGES - 1; page > 0; page--) {
            const uint16_t pos     = FRAMEBUFFER_START + (OLED_WIDTH * page) + xPos;
            const uint16_t prevPos = pos - OLED_WIDTH;
            screenBuffer[pos]      = (screenBuffer[pos] << 1) | ((screenBuffer[prevPos] & 0x80) >> 7);
          }
          // The top strip takes the next row of the new frame
          const uint16_t firstPos = FRAMEBUFFER_START + xPos;
          screenBuffer[firstPos]  = (screenBuffer[firstPos] << 1) | incoming;
        }
      }
    }

    if (rows < OLED_HEIGHT) {
      // The scroll indicator belongs to the new window. Pin the right-most
      // column to the new frame so old and new indicator segments do not scroll
      // together and show as a broken bar during the row transition.
      for (uint8_t page = 0; page < OLED_PAGES; page++) {
        const uint16_t pos = FRAMEBUFFER_START + (OLED_WIDTH * page) + (OLED_WIDTH - 1);
        screenBuffer[pos]  = secondFrameBuffer[pos];
      }
    }

    buttonsReleased |= getButtonState() == BUTTON_NONE;
    if (getButtonState() != BUTTON_NONE && buttonsReleased) {
      // Exit early, but have to transition whole buffer
      memcpy(screenBuffer + FRAMEBUFFER_START, secondFrameBuffer + FRAMEBUFFER_START,
             sizeof(screenBuffer) - FRAMEBUFFER_START);
      refresh(); // Now refresh to write out the contents to the new page
      return;
    }
    // To keep things faster, only redraw every second line
    if (frame % refreshEvery == 0) {
      refresh(); // Now refresh to write out the contents to the new page
    }
    vTaskDelayUntil(&startDraw, framePeriod);
  }

  // A partial scroll is only exact when the menu window moved by one row. Copy
  // the complete new frame so skipped (invisible) entries cannot leave a mixed
  // image on screen. This is a no-op for the normal one-row movement.
  if (rows < OLED_HEIGHT) {
    memcpy(screenBuffer + FRAMEBUFFER_START, secondFrameBuffer + FRAMEBUFFER_START,
           sizeof(screenBuffer) - FRAMEBUFFER_START);
  }
}

void OLED::transitionScrollDown(const TickType_t viewEnterTime, uint8_t rows) {
  transitionScroll(true, rows, viewEnterTime);
}

void OLED::transitionScrollUp(const TickType_t viewEnterTime, uint8_t rows) {
  transitionScroll(false, rows, viewEnterTime);
}

void OLED::setRotation(bool leftHanded) {
#ifndef OLED_FLIP
  leftHanded = !leftHanded;
#endif /* OLED_FLIP */
  if (inLeftHandedMode == leftHanded) {
    return;
  }
#ifdef OLED_SEGMENT_MAP_REVERSED
  if (!leftHanded) {
    OLED_Setup_Array[9].val = 0xA1;
  } else {
    OLED_Setup_Array[9].val = 0xA0;
  }
#else
  if (leftHanded) {
    OLED_Setup_Array[9].val = 0xA1;
  } else {
    OLED_Setup_Array[9].val = 0xA0;
  }
#endif /* OLED_SEGMENT_MAP_REVERSED */
  // send command struct again with changes
  if (leftHanded) {
    OLED_Setup_Array[5].val = 0xC8; // c1?
  } else {
    OLED_Setup_Array[5].val = 0xC0;
  }
  I2C_CLASS::writeRegistersBulk(DEVICEADDR_OLED, OLED_Setup_Array,
                                sizeof(OLED_Setup_Array) / sizeof(OLED_Setup_Array[0]));
  osDelay(TICKS_10MS);
  inLeftHandedMode = leftHanded;

  screenBuffer[5] = inLeftHandedMode ? OLED_GRAM_START_FLIP : OLED_GRAM_START; // display is shifted by 32 in left
                                                                               // handed mode as driver ram is 128 wide
  screenBuffer[7] = inLeftHandedMode ? OLED_GRAM_END_FLIP
                                     : OLED_GRAM_END; // End address of the ram segment we are writing to (128 wide)
  screenBuffer[9] = inLeftHandedMode ? 0xC8 : 0xC0;
  // Force a screen refresh
  const int len = FRAMEBUFFER_START + (OLED_WIDTH * OLED_PAGES);
  I2C_CLASS::Transmit(DEVICEADDR_OLED, screenBuffer, len);
  osDelay(TICKS_10MS);
  checkDisplayBufferChecksum();
}

void OLED::setBrightness(uint8_t contrast) {
  if (OLED_Setup_Array[15].val != contrast) {
    OLED_Setup_Array[15].val = contrast;
    I2C_CLASS::writeRegistersBulk(DEVICEADDR_OLED, &OLED_Setup_Array[14], 2);
  }
}

void OLED::setInverseDisplay(bool inverse) {
  uint8_t normalInverseCmd = inverse ? 0xA7 : 0xA6;
  if (OLED_Setup_Array[21].val != normalInverseCmd) {
    OLED_Setup_Array[21].val = normalInverseCmd;
    I2C_CLASS::I2C_RegisterWrite(DEVICEADDR_OLED, 0x80, normalInverseCmd);
  }
}

// print a string to the current cursor location, len chars MAX
void OLED::print(const char *const str, FontStyle fontStyle, uint8_t len, const uint8_t soft_x_limit) {
  const uint8_t *next = reinterpret_cast<const uint8_t *>(str);
  if (next[0] == 0x01) {
    fontStyle = FontStyle::LARGE;
    next++;
  }
  while (next[0] && len--) {
    uint16_t index;
    if (next[0] <= 0xF0) {
      index = next[0];
      next++;
    } else {
      if (!next[1]) {
        return;
      }
      index = (next[0] - 0xF0) * 0xFF - 15 + next[1];
      next += 2;
    }
    drawChar(index, fontStyle, soft_x_limit);
  }
}

/**
 * Prints a static string message designed to use the whole screen, starting
 * from the top-left corner.
 *
 * If the message starts with a newline (`\\x01`), the string starting from
 * after the newline is printed in the large font. Otherwise, the message
 * is printed in the small font.
 *
 * @param string The string message to be printed
 */
void OLED::printWholeScreen(const char *string) {
  setCursor(0, 0);
  if (string[0] == '\x01') {
    // Empty first line means that this uses large font (for CJK).
    OLED::print(string + 1, FontStyle::LARGE);
  } else {
    OLED::print(string, FontStyle::SMALL);
  }
}

// Print *F or *C - in font style of Small, Large (by default) or Extra based on input arg
void OLED::printSymbolDeg(const FontStyle fontStyle) {
  switch (fontStyle) {
  case FontStyle::EXTRAS:
    // Picks *F or *C in ExtraFontChars[] from Font.h
    OLED::drawSymbol(getSettingValue(SettingsOptions::TemperatureInF) ? 0 : 1);
    break;
  case FontStyle::LARGE:
    OLED::print(getSettingValue(SettingsOptions::TemperatureInF) ? LargeSymbolDegF : LargeSymbolDegC, fontStyle);
    break;
  case FontStyle::SMALL:
  default:
    OLED::print(getSettingValue(SettingsOptions::TemperatureInF) ? SmallSymbolDegF : SmallSymbolDegC, fontStyle);
    break;
  }
}

inline void stripLeaderZeros(char *buffer, uint8_t places) {
  // Removing the leading zero's by swapping them to SymbolSpace
  // Stop 1 short so that we dont blank entire number if its zero
  for (int i = 0; i < (places - 1); i++) {
    if (buffer[i] == 2) {
      buffer[i] = LargeSymbolSpace[0];
    } else {
      return;
    }
  }
}

void OLED::drawHex(uint32_t x, FontStyle fontStyle, uint8_t digits) {
  // print number to hex
  for (uint_fast8_t i = 0; i < digits; i++) {
    uint16_t value = (x >> (4 * (7 - i))) & 0b1111;
    drawChar(value + 2, fontStyle, 0);
  }
}

// maximum places is 5
void OLED::printNumber(uint16_t number, uint8_t places, FontStyle fontStyle, bool noLeaderZeros) {
  char buffer[7] = {0};

  if (places >= 5) {
    buffer[5] = 2 + number % 10;
    number /= 10;
  }
  if (places > 4) {
    buffer[4] = 2 + number % 10;
    number /= 10;
  }

  if (places > 3) {
    buffer[3] = 2 + number % 10;
    number /= 10;
  }

  if (places > 2) {
    buffer[2] = 2 + number % 10;
    number /= 10;
  }

  if (places > 1) {
    buffer[1] = 2 + number % 10;
    number /= 10;
  }

  buffer[0] = 2 + number % 10;
  if (noLeaderZeros) {
    stripLeaderZeros(buffer, places);
  }
  print(buffer, fontStyle);
}

void OLED::debugNumber(int32_t val, FontStyle fontStyle) {
  if (abs(val) > 99999) {
    OLED::print(LargeSymbolSpace, fontStyle); // out of bounds
    return;
  }
  if (val >= 0) {
    OLED::print(LargeSymbolSpace, fontStyle);
    OLED::printNumber(val, 5, fontStyle);
  } else {
    OLED::print(LargeSymbolMinus, fontStyle);
    OLED::printNumber(-val, 5, fontStyle);
  }
}

void OLED::drawSymbol(uint8_t symbolID) {
  // draw a symbol to the current cursor location
  drawChar(symbolID, FontStyle::EXTRAS, 0);
}

// Draw an area, but y must be aligned on 0/8 offset
void OLED::drawArea(int16_t x, int8_t y, uint8_t width, uint8_t height, const uint8_t *ptr) {
  // Splat this from x->x+width in two strides
  if (x <= -width) {
    return; // cutoffleft
  }
  if (x > OLED_WIDTH) {
    return; // cutoff right
  }

  uint8_t visibleStart = 0;
  uint8_t visibleEnd   = width;

  // trimming to draw partials
  if (x < 0) {
    visibleStart -= x; // subtract negative value == add absolute value
  }
  if (x + width > OLED_WIDTH) {
    visibleEnd = OLED_WIDTH - x;
  }
  uint8_t rowsDrawn = 0;
  while (height > 0) {
    const uint8_t strip = static_cast<uint8_t>((y / 8) + rowsDrawn);
    if (strip >= drawPageCount) {
      break; // clipped at the bottom of the panel
    }
    for (uint8_t xx = visibleStart; xx < visibleEnd; xx++) {
      stripPointers[strip][x + xx] = ptr[xx + (rowsDrawn * width)];
    }
    height -= 8;
    rowsDrawn++;
  }
}

// Draw an area, but y must be aligned on 0/8 offset
// For data which has octets swapped in a 16-bit word.
void OLED::drawAreaSwapped(int16_t x, int8_t y, uint8_t width, uint8_t height, const uint8_t *ptr) {
  // Splat this from x->x+width in two strides
  if (x <= -width) {
    return; // cutoffleft
  }
  if (x > OLED_WIDTH) {
    return; // cutoff right
  }

  uint8_t visibleStart = 0;
  uint8_t visibleEnd   = width;

  // trimming to draw partials
  if (x < 0) {
    visibleStart -= x; // subtract negative value == add absolute value
  }
  if (x + width > OLED_WIDTH) {
    visibleEnd = OLED_WIDTH - x;
  }

  uint8_t rowsDrawn = 0;
  while (height > 0) {
    const uint8_t strip = static_cast<uint8_t>((y / 8) + rowsDrawn);
    if (strip >= drawPageCount) {
      break; // clipped at the bottom of the panel
    }
    for (uint8_t xx = visibleStart; xx < visibleEnd; xx += 2) {
      stripPointers[strip][x + xx]     = ptr[xx + 1 + (rowsDrawn * width)];
      stripPointers[strip][x + xx + 1] = ptr[xx + (rowsDrawn * width)];
    }
    height -= 8;
    rowsDrawn++;
  }
}

void OLED::fillArea(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t value) {
  // Splat this from x->x+wide in two strides
  if (x <= -wide) {
    return; // cutoffleft
  }
  if (x > OLED_WIDTH) {
    return; // cutoff right
  }

  uint8_t visibleStart = 0;
  uint8_t visibleEnd   = wide;

  // trimming to draw partials
  if (x < 0) {
    visibleStart -= x; // subtract negative value == add absolute value
  }
  if (x + wide > OLED_WIDTH) {
    visibleEnd = OLED_WIDTH - x;
  }

  uint8_t rowsDrawn = 0;
  while (height > 0) {
    const uint8_t strip = static_cast<uint8_t>((y / 8) + rowsDrawn);
    if (strip >= drawPageCount) {
      break; // clipped at the bottom of the panel
    }
    for (uint8_t xx = visibleStart; xx < visibleEnd; xx++) {
      stripPointers[strip][x + xx] = value;
    }
    height -= 8;
    rowsDrawn++;
  }
}

void OLED::drawFilledRect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool clear) {
  // Ensure coordinates are within bounds
  if (x0 >= OLED_WIDTH || y0 >= OLED_HEIGHT || x1 >= OLED_WIDTH || y1 >= OLED_HEIGHT) {
    return;
  }

  // Calculate the height in rows
  uint8_t startRow  = y0 / 8;
  uint8_t endRow    = y1 / 8;
  uint8_t startMask = 0xFF << (y0 % 8);
  uint8_t endMask   = 0xFF >> (7 - (y1 % 8));

  if (startRow >= drawPageCount) {
    return;
  }
  if (endRow >= drawPageCount) {
    endRow = drawPageCount - 1;
  }

  for (uint8_t row = startRow; row <= endRow; row++) {
    uint8_t mask = 0xFF;
    if (row == startRow) {
      mask &= startMask;
    }
    if (row == endRow) {
      mask &= endMask;
    }

    for (uint8_t x = x0; x <= x1; x++) {
      if (clear) {
        stripPointers[row][x] &= ~mask;
      } else {
        stripPointers[row][x] |= mask;
      }
    }
  }
}

void OLED::invertArea(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
  // Ensure coordinates are within bounds
  if (x0 >= OLED_WIDTH || y0 >= OLED_HEIGHT || x1 >= OLED_WIDTH || y1 >= OLED_HEIGHT) {
    return;
  }

  const uint8_t startRow  = y0 / 8;
  const uint8_t endRow    = y1 / 8;
  const uint8_t startMask = 0xFF << (y0 % 8);
  const uint8_t endMask   = 0xFF >> (7 - (y1 % 8));

  for (uint8_t row = startRow; row <= endRow && row < drawPageCount; row++) {
    uint8_t mask = 0xFF;
    if (row == startRow) {
      mask &= startMask;
    }
    if (row == endRow) {
      mask &= endMask;
    }

    for (uint8_t x = x0; x <= x1; x++) {
      stripPointers[row][x] ^= mask;
    }
  }
}

void OLED::drawHeatSymbol(uint32_t x10Watts) {
  // Draw symbol 14
  // Then draw over it, the bottom 5 pixels always stay. 8 pixels above that are
  // the levels masks the symbol nicely
  uint8_t state = 0;
  if (x10Watts > 0) {
    const int32_t limit = getX10WattageLimits();
    if (limit > 0) {
      // Measurements rarely reach 100% of the limit, so treat 90% as full scale.
      const uint32_t fullScaleWatts = ((uint32_t)limit * 9) / 10;
      if (fullScaleWatts > 0) {
        state = (x10Watts >= fullScaleWatts) ? 8 : (x10Watts * 8) / fullScaleWatts; // 0-> 8 range
      }
    }
  }
  // Then we want to draw down (16-(5+state)
  uint16_t cursor_x_temp = cursor_x;
  drawSymbol(14);
  /*
       / / / / /
      / / / / /
      +---------+
      |         |
      +---------+


      <- 14 px ->
      What we are doing is aiming to clear a section of the screen, down to the base depending on how close we are to
      90% of the wattage limit. Larger numbers mean more heat, so we clear less of the screen.
  */
  drawFilledRect(cursor_x_temp, 0, cursor_x_temp + 12, 2 + (8 - state), true);
}

bool OLED::isInitDone() { return initDone; }
