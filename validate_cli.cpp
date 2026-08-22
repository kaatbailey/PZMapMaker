// CLI over the MapValidator library. Output format matches MapValidator.java's
// main() so a run over the same cells can be diffed line-for-line.
//   pz_validate <mapDir> <mediaDir> <cell> [cell...]
#include "celldata.hpp"
#include "mapvalidator.hpp"
#include "tileindex.hpp"

#include <iostream>
#include <string>

using namespace pzformat;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: pz_validate <mediaDir> <mapDir> <cell> [cell...]\n";
        return 2;
    }
    const std::filesystem::path mediaDir = argv[1];
    const std::filesystem::path mapDir = argv[2];

    const TileIndex ti = TileIndex::load(mediaDir);

    int cells = 0, roomsChecked = 0, errors = 0, warnings = 0;

    for (int i = 3; i < argc; ++i) {
        const std::string cellName = argv[i];
        try {
            const auto lh = mapDir / (cellName + ".lotheader");
            const auto lp = mapDir / ("world_" + cellName + ".lotpack");
            CellData c = CellData::load(lp, lh);

            std::cout << "\n--- " << cellName << ": " << c.header().rooms.size()
                      << " rooms ---\n";
            const auto rep = MapValidator::validate(ti, c);
            for (const auto& f : rep.findings) {
                std::cout << (f.severity == MapValidator::Severity::Error ? "  ERROR  " : "  WARN   ")
                          << f.room << ": " << f.message << '\n';
            }
            roomsChecked += rep.roomsChecked;
            errors += rep.errors();
            warnings += rep.warnings();
            ++cells;
        } catch (const std::exception& e) {
            std::cout << "  skipped " << cellName << ": " << e.what() << '\n';
        }
    }

    std::cout << '\n' << std::string(60, '=') << '\n';
    std::cout << "TOTAL: " << cells << " cells, " << roomsChecked << " rooms checked, "
              << errors << " errors, " << warnings << " warnings\n";
    if (errors == 0 && warnings == 0) {
        std::cout << "All rooms pass validation.\n";
    } else if (errors == 0) {
        std::cout << "No errors. Warnings may be intentional (open-plan boundaries).\n";
    }
    std::cout << std::string(60, '=') << '\n';
    return 0;
}
