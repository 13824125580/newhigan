#include <gba/gba.hpp>

namespace GameBoyAdvance {

Cartridge cartridge;
#include "mrom.cpp"
#include "sram.cpp"
#include "eeprom.cpp"
#include "flash.cpp"
#include "serialization.cpp"
#include "game_gba.c"

Cartridge::Cartridge() {
  mrom.data = new uint8[mrom.size = 32 * 1024 * 1024];
  sram.data = new uint8[sram.size = 32 * 1024];
  eeprom.data = new uint8[eeprom.size = 8 * 1024];
  flash.data = new uint8[flash.size = 128 * 1024];
}

Cartridge::~Cartridge() {
  delete[] mrom.data;
  delete[] sram.data;
  delete[] eeprom.data;
  delete[] flash.data;
}

auto Cartridge::load() -> bool {
  hasSRAM   = false;
  hasEEPROM = false;
  hasFLASH  = false;

  mrom.size = min(32 * 1024 * 1024, game_gba_len);
  memcpy(mrom.data, game_gba, mrom.size);

  return true;
}

auto Cartridge::save() -> void {
}

auto Cartridge::unload() -> void {
}

auto Cartridge::power() -> void {
}

#define RAM_ANALYZE

auto Cartridge::read(uint mode, uint32 addr) -> uint32 {
  if(addr < 0x0e00'0000) {
    if(hasEEPROM && (addr & eeprom.mask) == eeprom.test) return eeprom.read();
    return mrom.read(mode, addr);
  } else {
    if(hasSRAM) return sram.read(mode, addr);
    if(hasFLASH) return flash.read(addr);
    return cpu.pipeline.fetch.instruction;
  }
}

auto Cartridge::write(uint mode, uint32 addr, uint32 word) -> void {
  if(addr < 0x0e00'0000) {
    if(hasEEPROM && (addr & eeprom.mask) == eeprom.test) return eeprom.write(word & 1);
    return mrom.write(mode, addr, word);
  } else {
    if(hasSRAM) return sram.write(mode, addr, word);
    if(hasFLASH) return flash.write(addr, word);
  }
}

}
