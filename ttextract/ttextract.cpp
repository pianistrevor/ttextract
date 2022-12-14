// ttextract.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
//

#include "ttextract.h"
#include "include\filereading.h"
#include "include\unlz2k.h"
#include <cctype>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

bool initialized = false;
char *outfileBuffer;

// Via http://www.isthe.com/chongo/tech/comp/fnv/
constexpr unsigned int FNV_BASIS = 2166136261;
// constexpr unsigned int FNV_PRIME = 16777619;
constexpr unsigned int FNV_PRIME = 1677619;

constexpr std::string nameOfAlg(int packedType) {
  switch (packedType) {
    using enum algType;
  case 0:
    return "----";
  case 2:
    return "LZ2K";
  default:
    return "????";
  }
}

const std::vector<int> validSignatures{-1, -2, -3, -4};

void init() {
  // 1 MB file output buffer
  outfileBuffer = new char[0x100000];
}

int main(int argc, char *argv[]) {
  if (!initialized) {
    init();
    initialized = true;
  }
  cmdlineArgs args;
  try {
    args = parseArgs(argc, argv);
  } catch (int errorCode) {
    return errorCode;
  }
  std::ifstream in(static_cast<std::string>(args.fileName),
                   std::ios::in | std::ios::binary);
  if (!in) {
    std::cerr << "Cannot open source file.\n";
    return 1;
  }
  if (args.isUnpack) {
    // TODO: Replace
    std::ofstream dest(args.outName, std::ios::out | std::ios::binary);
    unlz2k(in, dest);
    return 0;
  }
  in.seekg(0, std::ios_base::end);
  size_t fileSize = in.tellg();
  in.seekg(0);
  // First word of file should determine what type of file we're extracting
  int firstWord = readUint32(in, ENDIAN::little);
  if (firstWord == 0x12345678) {
    // Assume .FPK
    try {
      handleFPK(in, fileSize, args);
    } catch (int errorCode) {
      std::cerr << "Program exited with code " << errorCode << '\n';
      return errorCode;
    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what();
      return 1;
    }
    return 0;
  } else {
    // Assume .DAT
    uint32_t fileInfoOffset = firstWord;
    // If larger than end of file, try 2s complement >> 8
    if (firstWord > fileSize) {
      fileInfoOffset = (~fileInfoOffset + 1) << 8;
    }
    try {
      handleDAT(in, fileSize, args, fileInfoOffset);
    } catch (int errorCode) {
      std::cerr << "Program exited with code " << errorCode << '\n';
      return errorCode;
    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what();
      return 1;
    }
    return 0;
  }
  logError("Unrecognized archive type.");
  return 1;
}

std::string readName(std::ifstream &src, size_t offsetInFile) {
  auto currOffset = src.tellg();
  src.seekg(offsetInFile);
  std::string itemName = "";
  char buffer;
  src.read(&buffer, 1);
  while (buffer) {
    itemName += buffer;
    src.read(&buffer, 1);
  }
  src.seekg(currOffset);
  return itemName;
}

void handleFPK(std::ifstream &src, size_t fileSize, cmdlineArgs &args) {
  return;
}

void handleDAT(std::ifstream &src, size_t fileSize, cmdlineArgs &args,
               unsigned int firstWord) {

  // First, the file info section
  auto fileInfoOffset = firstWord;
  auto fileInfoSize = readUint32(src, ENDIAN::little);
  auto expectedSize = fileInfoOffset + fileInfoSize;
  if (expectedSize != fileSize) {
    std::cerr << std::format("Size mismatch. Expected 0x{:<8X}, got 0x{:<8X}",
                             expectedSize, fileSize);
    throw 1;
  }
  src.seekg(fileInfoOffset);
  auto signature = readInt32(src, ENDIAN::little);
  if (!std::count(validSignatures.begin(), validSignatures.end(), signature)) {
    std::cerr << std::format("File signature {} invalid.\n", signature);
    throw 1;
  }
  std::cout << "DAT file with signature: " << signature << '\n';
  auto numFiles = readUint32(src, ENDIAN::little);
  fileInfoOffset += 8;
  std::cout << std::format("File info offset: 0x{:<8X}\n", fileInfoOffset);
  std::cout << std::format("File info size: 0x{:<8X}\n", fileInfoSize);
  std::cout << std::format("Number of files: {}\n", numFiles);

  // Name info section
  auto nameInfoOffset = fileInfoOffset + (numFiles << 4);
  src.seekg(nameInfoOffset);
  auto numNames = readUint32(src, ENDIAN::little);
  nameInfoOffset += 4;
  std::cout << std::format("Name info offset: 0x{:<8X}\n", nameInfoOffset);
  std::cout << std::format("Number of names: {}\n", numNames);

  // Name data section
  auto nameDataOffset = nameInfoOffset + (numNames << 3);
  src.seekg(nameDataOffset);
  auto nameCRCOffset = readUint32(src, ENDIAN::little);
  nameDataOffset += 4;
  nameCRCOffset += nameDataOffset;
  std::cout << std::format("Name data offset: 0x{:<8X}\n", nameDataOffset);
  std::cout << std::format("Name CRC offset: 0x{:<8X}\n", nameCRCOffset);

  // CRC section
  std::unordered_map<unsigned int, unsigned short int> crcToIndex;
  bool hasCRCs = false;
  src.seekg(nameCRCOffset);
  if (src.tellg() != fileSize) {
    auto first = readUint32(src, ENDIAN::little);
    if (first) {
      hasCRCs = true;
      crcToIndex[first] = 0;
      for (uint32_t i = 1; i < numFiles; ++i) {
        auto crc = readUint32(src, ENDIAN::little);
        crcToIndex[crc] = i;
      }
      // Should have two dwords left, expecting zeroes
      unsigned int end = readUint32(src, ENDIAN::little);
      end += readUint32(src, ENDIAN::little);
      if (end) {
        logError(std::format("Unexpected non-zero bytes at 0x{:<8X}\n",
                             static_cast<int>(src.tellg()) - 8));
        throw 1;
      }
      if (src.tellg() != fileSize) {
        logError(std::format("Unexpected non-zero data at 0x{:<8X}\n",
                             static_cast<int>(src.tellg())));
        throw 1;
      }
    }
  }
  std::cout << std::format("Number of CRCs: {}\n\n", crcToIndex.size());
  std::cout << "Offset  \tPacked  \tUnpacked\tAlg?\tFile\n";
  std::cout << std::string(100, '-') << '\n';
  /*
  Now the extraction section!

  Name info table items are 8 bytes long.

  First value is signed 16 bit.
      If positive, read folder name. Value is index for last item in folder.
      If negative or zero, read file name. Value is 2s complement of file ID.

  Second value is signed 16 bit.
      If positive, use previous file path. Value is index of previous item
  written in directory If zero, do not use previous file path. Write item to
  current directory.

  Third value is relative 32-bit offset to read name from. This is added to the
  name data offset.
  */

  src.seekg(nameInfoOffset);
  uint16_t lastItemOffset = 0;
  std::string currentDir{""};
  std::unordered_map<uint16_t, std::string> itemDirs;
  for (uint16_t nameOffset = 0; nameOffset < numNames; ++nameOffset) {
    bool isFolder = false, isPacked = false;
    std::string itemName{""};
    int fileID = -1;
    int16_t readType = readInt16(src, ENDIAN::little);
    if (readType > 0) {
      lastItemOffset = readType;
      isFolder = true;
    } else {
      fileID = readType * -1;
    }
    int16_t pathType = readInt16(src, ENDIAN::little);
    if (pathType > 0) {
      itemName = itemDirs.at(pathType);
    } else {
      itemName = currentDir;
    }
    itemDirs[nameOffset] = itemName;
    uint32_t nameDataRelativeOffset = readUint32(src, ENDIAN::little);

    // Get name
    auto name = readName(src, static_cast<size_t>(nameDataOffset) +
                                      nameDataRelativeOffset);
    if (!name.empty()) {
      itemName += '\\';
    }
    itemName += name;

    if (isFolder) {
      if (!itemName.empty()) {
        currentDir = itemName;
      }
    } else {
      uint32_t fileIndex;
      if (hasCRCs) {
        auto analysisName = itemName.substr(1);
        uint32_t crc = FNV_BASIS;
        for (auto &c : analysisName) {
          crc = (crc ^ toupper(c)) * FNV_PRIME & 0xFFFFFFFF;
        }
        try {
          fileIndex = crcToIndex.at(crc);
        } catch (std::out_of_range error) {
          logError(
              std::format("CRC 0x{:<8X} doesn't correspond to a file", crc));
          throw 1;
        }
      } else {
        // Extract current file
        fileIndex = fileID;
      }
      /*
      File info table items are 16 bytes long.

      4 bytes = absolute offset of data
          Signature other than -1 shifts left 8 bits
      4 bytes = size of file (archived)
      4 bytes = size of file (uncompressed)
      1 byte  = packed type
      2 bytes = unknown??
      1 byte  = byte offset from the given offset
      */
      uint32_t offset;
      uint32_t packedSize;
      uint32_t unpackedSize;
      uint32_t packedType;
      fileIndex = fileInfoOffset + (fileIndex << 4);
      auto prevOffset = src.tellg();
      src.seekg(fileIndex);
      offset = readUint32(src, ENDIAN::little);
      if (signature != -1) {
        offset <<= 8;
      }
      packedSize = readUint32(src, ENDIAN::little);
      unpackedSize = readUint32(src, ENDIAN::little);
      auto nextFour = readUint32(src, ENDIAN::little);
      packedType = nextFour & 0xFF;
      offset += nextFour >> 24;
      isPacked = packedSize != unpackedSize;
      std::string outputDir = args.outDir + currentDir;
      std::string outputItem = args.outDir + itemName;
      std::cout << std::format("{:0>8X}\t{:<8X}\t{:<8X}\t{}\t{}\n", offset,
                               packedSize, unpackedSize, nameOfAlg(packedType),
                               outputItem);
      if (!std::filesystem::is_directory(outputDir)) {
        std::filesystem::create_directories(outputDir);
      }
      std::ofstream dest(outputItem, std::ios::out | std::ios::binary);
      if (!dest) {
        logError("Error opening destination file.");
        throw 1;
      }
      src.seekg(offset);
      if (!args.isRaw && isPacked) {
        if (packedType == 2) {
          unlz2k(src, dest, packedSize, unpackedSize);
        } else {
          logWarning(std::format("Unknown packed type {}, unpacking raw file",
                                 packedType));
          writeToDest(src, dest, packedSize);
        }
      } else {
        writeToDest(src, dest, packedSize);
      }
      // Return to previous offset
      src.seekg(prevOffset);
    }
  }
}

void writeToDest(std::ifstream &src, std::ofstream &dest, size_t numBytes) {
  while (numBytes > 0) {
    size_t toWrite = numBytes < 0x100000 ? numBytes : 0x100000;
    src.read(outfileBuffer, toWrite);
    dest.write(outfileBuffer, toWrite);
    numBytes -= toWrite;
  }
}