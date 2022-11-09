#ifndef TTEXTRACT_H
#define TTEXTRACT_H
#include <string>
#include <fstream>
#include <istream>

enum class argType { HELP, UNPACK, SIZE, PACKED, ALG, RAW, DIRECTORY, OUTFILE };

enum class algType { UNSPECIFIED, NONE, LZ2K };

struct cmdlineArgs {
    std::string_view fileName{ "" };
    bool isArchive{ true };
    bool isUnpack{ false };
    bool isRaw{ false };
    int size{ -1 };
    int packedSize{ -1 };
    algType alg{ algType::UNSPECIFIED };
    std::string outDir{ "" };
    std::string outName{ "" };
};

cmdlineArgs parseArgs(int argc, char *argv[]);

void printHelpMessage();
void logWarning(std::string message);
void logError(std::string message);

void handleFPK(std::ifstream& in, size_t fileSize, cmdlineArgs &args);
void handleDAT(std::ifstream& in, size_t fileSize, cmdlineArgs &args, unsigned int firstWord);
void writeToDest(std::ifstream& src, std::ofstream& dest, size_t numBytes);

#endif // TTEXTRACT_H
