/*
 * data-source-dos.cc - DOS game resources file functions
 *
 * Copyright (C) 2014-2019  Jon Lund Steffensen <jonlst@gmail.com>
 *
 * This file is part of freeserf.
 *
 * freeserf is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * freeserf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with freeserf.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "src/data-source-dos.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

#include "src/freeserf_endian.h"
#include "src/log.h"
#include "src/tpwm.h"
#include "src/data.h"
#include "src/sfx2wav.h"
#include "src/xmi2mid.h"

#define DATA_SERF_ANIMATION_TABLE  2
#define DATA_SERF_ARMS             1850  // 3, dos_sprite_type_transparent
#define DATA_SFX_BASE              3900  // SFX sounds (index 0 is undefined)
#define DATA_MUSIC_GAME            3990  // XMI music
#define DATA_MUSIC_ENDING          3992  // XMI music

// There are different types of sprites:
// - Non-packed, rectangular sprites: These are simple called sprites here.
// - Transparent sprites, "transp": These are e.g. buldings/serfs.
// The transparent regions are RLE encoded.
// - Bitmap sprites: Conceptually these contain either 0 or 1 at each pixel.
// This is used to either modify the alpha level of another sprite (shadows)
// or mask parts of other sprites completely (mask sprites).

DataSourceDOS::Resource dos_resources[] = {
  {   0, 0,    DataSourceDOS::SpriteTypeUnknown     },  // none
  {   1, 3997, DataSourceDOS::SpriteTypeSolid       },  // art_landscape
  {   2, 0,    DataSourceDOS::SpriteTypeUnknown     },  // animation
  {   4, 3,    DataSourceDOS::SpriteTypeOverlay     },  // serf_shadow
  {   5, 3,    DataSourceDOS::SpriteTypeSolid       },  // dotted_lines
  {  15, 3997, DataSourceDOS::SpriteTypeSolid       },  // art_flag
  {  25, 3,    DataSourceDOS::SpriteTypeSolid       },  // art_box
  {  40, 3998, DataSourceDOS::SpriteTypeSolid       },  // credits_bg
  {  41, 3998, DataSourceDOS::SpriteTypeSolid       },  // logo
  {  42, 3,    DataSourceDOS::SpriteTypeSolid       },  // symbol
  {  60, 3,    DataSourceDOS::SpriteTypeMask        },  // map_mask_up
  { 141, 3,    DataSourceDOS::SpriteTypeMask        },  // map_mask_down
  { 230, 3,    DataSourceDOS::SpriteTypeMask        },  // path_mask
  { 260, 3,    DataSourceDOS::SpriteTypeSolid       },  // map_ground
  { 300, 3,    DataSourceDOS::SpriteTypeSolid       },  // path_ground
  { 321, 3,    DataSourceDOS::SpriteTypeTransparent },  // game_object
  { 600, 3,    DataSourceDOS::SpriteTypeSolid       },  // frame_top
  { 610, 3,    DataSourceDOS::SpriteTypeTransparent },  // map_border
  { 630, 3,    DataSourceDOS::SpriteTypeTransparent },  // map_waves
  { 660, 3,    DataSourceDOS::SpriteTypeSolid       },  // frame_popup
  { 670, 3,    DataSourceDOS::SpriteTypeSolid       },  // indicator
  { 750, 3,    DataSourceDOS::SpriteTypeTransparent },  // font
  { 810, 3,    DataSourceDOS::SpriteTypeTransparent },  // font_shadow
  { 870, 3,    DataSourceDOS::SpriteTypeSolid       },  // icon
  {1250, 3,    DataSourceDOS::SpriteTypeTransparent },  // map_object
  {1500, 3,    DataSourceDOS::SpriteTypeOverlay     },  // map_shadow
  {1750, 3,    DataSourceDOS::SpriteTypeSolid       },  // panel_button
  {1780, 3,    DataSourceDOS::SpriteTypeSolid       },  // frame_bottom
  {2500, 3,    DataSourceDOS::SpriteTypeTransparent },  // serf_torso
  {3150, 3,    DataSourceDOS::SpriteTypeTransparent },  // serf_head
  {3880, 3,    DataSourceDOS::SpriteTypeSolid       },  // frame_split
  {3900, 0,    DataSourceDOS::SpriteTypeUnknown     },  // sound
  {3990, 0,    DataSourceDOS::SpriteTypeUnknown     },  // music
  {3999, 3,    DataSourceDOS::SpriteTypeTransparent },  // cursor
  {   3, 0,    DataSourceDOS::SpriteTypeUnknown     }   // palette
};

DataSourceDOS::DataSourceDOS(const std::string &_path)
  : DataSourceLegacy(_path) {
}

DataSourceDOS::~DataSourceDOS() {
}

bool
DataSourceDOS::check() {
  std::vector<std::string> default_file_names = {
    "SPAE.PA",  // English
    "SPAF.PA",  // French
    "SPAD.PA",  // German
    "SPAU.PA"   // Engish (US)
  };

  if (check_file(path)) {
    return true;
  }

  for (std::string file_name : default_file_names) {
    std::string file_path = path + '/' + file_name;
    Log::Info["data"] << "Looking for game data in '" << file_path << "'...";
    if (check_file(file_path)) {
      path = std::move(file_path);
      return true;
    }
  }

  return false;
}

bool
DataSourceDOS::load() {
  if (!check()) {
    return false;
  }

  try {
    spae = std::make_shared<Buffer>(path);
  } catch (...) {
    return false;
  }

  // Check that data file is decompressed
  try {
    UnpackerTPWM unpacker(spae);
    spae = unpacker.convert();
    Log::Verbose["data"] << "Data file is compressed";
  }  catch (...) {
    Log::Verbose["data"] << "Data file is not compressed";
  }

  // Read the number of entries in the index table.
  // Some entries are undefined (size and offset are zero).
  size_t entry_count = spae->pop<uint32_t>();
  entries.push_back({0, 0});  // first entry is whole file itself, drop it
  for (size_t i = 0; i < entry_count; i++) {
    DataEntry entry;
    entry.size = spae->pop<uint32_t>();
    entry.offset = spae->pop<uint32_t>();
    entries.push_back(entry);
  }

  fixup();

  // The first uint32 is the byte length of the rest
  // of the table in big endian order.
  PBuffer anim = get_object(DATA_SERF_ANIMATION_TABLE);
  anim->set_endianess(Buffer::EndianessBig);
  size_t size = anim->get_size();
  if (size != anim->pop<uint32_t>()) {
    Log::Error["data"] << "Could not extract animation table.";
    return false;
  }
  anim = anim->pop_tail();

  return load_animation_table(anim);
}

// Return buffer with object at index
PBuffer
DataSourceDOS::get_object(size_t index) {
  if (index >= entries.size()) {
    return nullptr;
  }

  if (entries[index].offset == 0) {
    return nullptr;
  }

  return spae->get_subbuffer(entries[index].offset, entries[index].size);
}

// Perform various fixups of the data file entries
void
DataSourceDOS::fixup() {
  // Fill out some undefined spaces in the index from other
  // places in the data file index.

  for (int i = 0; i < 48; i++) {
    for (int j = 1; j < 6; j++) {
      entries[3450+6*i+j] = entries[3450+6*i];
    }
  }

  for (int i = 0; i < 3; i++) {
    entries[3765+i] = entries[3762+i];
  }

  for (int i = 0; i < 6; i++) {
    entries[1363+i] = entries[1352];
    entries[1613+i] = entries[1602];
  }
}

// Create sprite object
Data::MaskImage
DataSourceDOS::get_sprite_parts(Data::Resource res, size_t index) {
  if (index >= Data::get_resource_count(res)) {
    return std::make_tuple(nullptr, nullptr);
  }

  Resource &dos_res = dos_resources[res];

  ColorDOS *palette = get_dos_palette(dos_res.dos_palette);
  if (palette == nullptr) {
    return std::make_tuple(nullptr, nullptr);
  }

  if (res == Data::AssetSerfTorso) {
    PBuffer data = get_object(dos_res.index + index);
    if (!data) {
      return std::make_tuple(nullptr, nullptr);
    }
    Data::PSprite torso = std::make_shared<SpriteDosTransparent>(data, palette,
                                                                 64);

    data = get_object(dos_res.index + index);
    if (!data) {
      return std::make_tuple(nullptr, nullptr);
    }
    Data::PSprite torso2 = std::make_shared<SpriteDosTransparent>(data, palette,
                                                                  72);

    Data::MaskImage mi = separate_sprites(torso, torso2);

    data = get_object(DATA_SERF_ARMS + index);
    Data::PSprite arms = std::make_shared<SpriteDosTransparent>(data, palette);
    torso->stick(arms, 0, 0);

    return mi;
  } else if (res == Data::AssetMapObject) {
      if ((index >= 128) && (index <= 143)) {  // Flag sprites
      int flag_frame = (index - 128) % 4;
      PBuffer data = get_object(dos_res.index + 128 + flag_frame);
      if (!data) {
        return std::make_tuple(nullptr, nullptr);
      }
      Data::PSprite s1 = std::make_shared<SpriteDosTransparent>(data, palette);
      data = get_object(dos_res.index + 128 + 4 + flag_frame);
      if (!data) {
        return std::make_tuple(nullptr, nullptr);
      }
      Data::PSprite s2 = std::make_shared<SpriteDosTransparent>(data, palette);

      return separate_sprites(s1, s2);
    }
  } else if (res == Data::AssetFont || res == Data::AssetFontShadow) {
    PBuffer data = get_object(dos_res.index + index);
    if (!data) {
      return std::make_tuple(nullptr, nullptr);
    }
    return std::make_tuple(std::make_shared<SpriteDosTransparent>(data,
                                                                  palette),
                           nullptr);
  }

  PBuffer data = get_object(dos_res.index + index);
  if (!data) {
    return std::make_tuple(nullptr, nullptr);
  }

  Data::PSprite sprite;
  switch (dos_res.sprite_type) {
    case SpriteTypeSolid: {
      sprite = std::make_shared<SpriteDosSolid>(data, palette);
      break;
    }
    case SpriteTypeTransparent: {
      sprite = std::make_shared<SpriteDosTransparent>(data, palette);
      break;
    }
    case SpriteTypeOverlay: {
      sprite = std::make_shared<SpriteDosOverlay>(data, palette, 0x80);
      break;
    }
    case SpriteTypeMask: {
      sprite = std::make_shared<SpriteDosMask>(data);
      break;
    }
    default:
      return std::make_tuple(nullptr, nullptr);
  }

  return std::make_tuple(nullptr, sprite);
}

DataSourceDOS::SpriteDosSolid::SpriteDosSolid(PBuffer _data, ColorDOS *palette)
  : SpriteBaseDOS(_data) {
  size_t size = _data->get_size();
  if (size != (width * height + 10)) {
    throw ExceptionFreeserf("Failed to extract DOS solid sprite");
  }

  PMutableBuffer result = std::make_shared<MutableBuffer>(Buffer::EndianessBig);

  while (_data->readable()) {
    ColorDOS color = palette[_data->pop<uint8_t>()];
    result->push<uint8_t>(color.b);  // Blue
    result->push<uint8_t>(color.g);  // Green
    result->push<uint8_t>(color.r);  // Red
    result->push<uint8_t>(0xff);     // Alpha
  }

  data = reinterpret_cast<uint8_t*>(result->unfix());
}

DataSourceDOS::SpriteDosTransparent::SpriteDosTransparent(PBuffer _data,
                                                          ColorDOS *palette,
                                                          uint8_t color)
  : SpriteBaseDOS(_data) {
  PMutableBuffer result = std::make_shared<MutableBuffer>(Buffer::EndianessBig);

  while (_data->readable()) {
    size_t drop = _data->pop<uint8_t>();
    result->push<uint32_t>(0x00000000, drop);

    size_t fill = _data->pop<uint8_t>();
    for (size_t i = 0; i < fill; i++) {
      unsigned int p_index = _data->pop<uint8_t>() + color;  // color_off;
      ColorDOS color = palette[p_index];
      result->push<uint8_t>(color.b);  // Blue
      result->push<uint8_t>(color.g);  // Green
      result->push<uint8_t>(color.r);  // Red
      result->push<uint8_t>(0xFF);     // Alpha
    }
  }

  data = reinterpret_cast<uint8_t*>(result->unfix());
}

DataSourceDOS::SpriteDosOverlay::SpriteDosOverlay(PBuffer _data,
                                                  ColorDOS *palette,
                                                  unsigned char value)
  : SpriteBaseDOS(_data) {
  PMutableBuffer result = std::make_shared<MutableBuffer>(Buffer::EndianessBig);

  while (_data->readable()) {
    size_t drop = _data->pop<uint8_t>();
    result->push<uint32_t>(0x00000000, drop);

    size_t fill = _data->pop<uint8_t>();
    for (size_t i = 0; i < fill; i++) {
      ColorDOS color = palette[value];
      result->push<uint8_t>(color.b);  // Blue
      result->push<uint8_t>(color.g);  // Green
      result->push<uint8_t>(color.r);  // Red
      result->push<uint8_t>(value);    // Alpha
    }
  }

  data = reinterpret_cast<uint8_t*>(result->unfix());
}

DataSourceDOS::SpriteDosMask::SpriteDosMask(PBuffer _data)
  : SpriteBaseDOS(_data) {
  PMutableBuffer result = std::make_shared<MutableBuffer>(Buffer::EndianessBig);

  while (_data->readable()) {
    size_t drop = _data->pop<uint8_t>();
    result->push<uint32_t>(0x00000000, drop);

    size_t fill = _data->pop<uint8_t>();
    result->push<uint32_t>(0xFFFFFFFF, fill);
  }

  data = reinterpret_cast<uint8_t*>(result->unfix());
}


DataSourceDOS::SpriteBaseDOS::SpriteBaseDOS(PBuffer _data) {
  if (_data->get_size() < 10) {
    throw ExceptionFreeserf("Failed to extract DOS sprite");
  }

  delta_x = _data->pop<int8_t>();
  delta_y = _data->pop<int8_t>();
  width = _data->pop<uint16_t>();
  height = _data->pop<uint16_t>();
  offset_x = _data->pop<int16_t>();
  offset_y = _data->pop<int16_t>();
}

PBuffer
DataSourceDOS::get_sound(size_t index) {
  PBuffer data = get_object(DATA_SFX_BASE + index);
  if (!data) {
    Log::Error["data"] << "Could not extract SFX clip: #" << index;
    return nullptr;
  }

  try {
    ConvertorSFX2WAV convertor(data, -32);
    return convertor.convert();
  } catch (...) {
    Log::Error["data"] << "Could not convert SFX clip to WAV: #" << index;
    return nullptr;
  }
}

PBuffer
DataSourceDOS::get_music(size_t index) {
  PBuffer data = get_object(DATA_MUSIC_GAME + index);
  if (!data) {
    Log::Error["data"] << "Could not extract XMI clip: #" << index;
    return nullptr;
  }

  try {
    ConvertorXMI2MID convertor(data);
    return convertor.convert();
  } catch (...) {
    Log::Error["data"] << "Could not convert XMI clip to MID: #" << index;
    return nullptr;
  }
}

DataSourceDOS::ColorDOS *
DataSourceDOS::get_dos_palette(size_t index) {
  PBuffer data = get_object(index);
  if (!data || (data->get_size() != sizeof(ColorDOS)*256)) {
    return nullptr;
  }

  return reinterpret_cast<ColorDOS*>(data->get_data());
}
