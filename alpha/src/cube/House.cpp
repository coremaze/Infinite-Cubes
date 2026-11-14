#include "cube/WorldDimensions.h"
// House implements the cell layout used to generate buildings.
#include "cube/House.h"

#include "cube/WorldBlocks.h"
#include "cube/CombatBehavior.h"
#include "cube/WorldGeneration.h"
#include "cube/ItemIds.h"
#include "cube/LookAtPlayerBehavior.h"
#include "cube/Noise.h"
#include "cube/RaceIds.h"
#include "cube/RandomInteractionBehavior.h"
#include "cube/RandomWalkBehavior.h"
#include "cube/SequentialBehavior.h"
#include "cube/SpawnLocationBehavior.h"
#include "cube/Spawn.h"
#include "cube/Sprite.h"
#include "cube/SpriteIds.h"
#include "cube/WalkPathBehavior.h"
#include <algorithm>
#include <math.h>
#include <stdlib.h>

namespace cube {

House::House(int sizeX, int sizeY, int sizeZ)
    : m_rotation(0),
      m_reflectY(false),
      unused18(-1),
      unused1c(-1),
      unused20(0),
      variant(HouseVariantHome),
      m_sizeX(sizeX),
      m_sizeY(sizeY),
      m_sizeZ(sizeZ)
{
    m_cells = new Cell[sizeX * sizeY * sizeZ];
}

House::~House()
{
    delete[] m_cells;
}

// Lays out the houses, market, fields and special buildings of a region record's settlement in a
// zone: the zone's 256 x 256 columns are divided into count x count plots (five per axis, four in
// record mode 5), each plot gets a kind from its terrain and the record's influence, and each kind
// is then built. Returns false when no plot can hold a house.
namespace {
enum {
    InteriorTopInitialSentinel = -10000
};
enum {
    InteriorRoadMargin = 7
};
enum {
    InteriorHeightSampleMargin = 8
};
enum {
    MaximumPlotRelief = 16
};
enum {
    CityPlotRemovalAttempts = 6
};
enum {
    CornflowerChanceDenominator = 10
};
enum {
    CropPlacementChanceDenominator = 10
};
enum {
    ProduceChanceDenominator = 4
};
enum {
    FencePostOmissionDenominator = 10
};
enum {
    FenceVariantCount = 4
};
enum {
    HouseCellWidth = 13
};
enum {
    HouseCellCenterOffset = 7
};
enum {
    HouseFoundationDepth = 6
};
enum {
    HouseRoadBorderWidth = 2
};
enum {
    RaisedRoadSurfaceOffset = 6
};
enum {
    HouseStoreyHeight = 7
};
enum {
    WallDecorationChanceDenominator = 6
};
enum {
    EntranceStairVerticalOffset = 4
};
enum {
    CarpetVariantCount = 3
};
enum {
    InteriorFurnitureChanceDenominator = 3
};
enum {
    PlainRoomTableChanceDenominator = 5
};
enum {
    OpenPlotAnimalChanceDenominator = 8
};
enum {
    AntiqueBuildingVariantCount = 4
};
enum {
    PlazaInnerRadiusSquared = 49
};
enum {
    PlazaBenchChanceDenominator = 8
};
enum {
    FallbackMaximumVendorLevel = 20
};
enum {
    PetFoodSpeechChanceDenominator = 10
};
enum {
    FirstShopVisitDurationRange = 20
};
enum {
    LaterShopVisitDurationRange = 30
};
enum {
    OpenPlotGuardChanceDenominator = 5
};
enum {
    GuardRingCountRange = 3
};
enum {
    VillagerDepartureTimeRangeMinutes = 180
};
enum {
    VillagerEarliestDepartureMinutes = 420
};
enum {
    SquareOrHomeVisitRangeMinutes = 60
};
enum {
    MinimumVisitMinutes = 60
};
enum {
    MarketOrInnVisitRangeMinutes = 180
};
enum {
    MinimumRemainingHousePlots = 3
};
}

bool World::generateHouse(Zone *zone, Region::Record *record)
{
    const int count = record->kind == Region::Record::SceneryRuins ? 4 : 5;
    // The animals that roam the plots without houses.
    std::vector<int> wildRaces;
    wildRaces.push_back(RaceSheep);
    wildRaces.push_back(RaceCat);
    wildRaces.push_back(RaceCollie);
    wildRaces.push_back(RaceTerrier);
    wildRaces.push_back(RacePig);

    const int plotSize = BlocksPerZone / count;
    std::vector<HousePlot> plots;
    plots.resize(count * count);

    // Sample every plot's columns: their height range, the excluded materials (water and type 3) and
    // type-11 ground inside the plot.
    for (int x = 0; x < count; ++x) {
        for (int y = 0; y < count; ++y) {
            bool first = true;
            bool rejected = false;
            bool hasInfluence = false;
            bool hasInteriorRoad = false;
            int minimum = 0;
            int maximum = 0;
            int interiorTop = InteriorTopInitialSentinel;
            for (int dx = 0; dx < plotSize; ++dx) {
                for (int dy = 0; dy < plotSize; ++dy) {
                    const int blockX = zone->m_position[0] * BlocksPerZone + x * BlocksPerZone / count + dx;
                    const int blockY = zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count + dy;
                    float score = record->influence(Fixed(blockX), Fixed(blockY));
                    if (score > 0.1f)
                        hasInfluence = true;
                    Field *field = getField(blockX, blockY, zone);
                    if (field->m_baseZ + field->m_size <= 0)
                        rejected = true;
                    for (int z = 0; z < field->m_size; ++z) {
                        if ((field->getMutableBlock(z).type & ~BlockFlagMask) == BlockWater ||
                            (field->getMutableBlock(z).type & ~BlockFlagMask) == BlockWet)
                            rejected = true;
                        if (dx > InteriorRoadMargin && dy > InteriorRoadMargin && dx < plotSize - InteriorRoadMargin &&
                            dy < plotSize - InteriorRoadMargin && score < 0.65f) {
                            if ((field->getMutableBlock(z).type & ~BlockFlagMask) == BlockRoad)
                                hasInteriorRoad = true;
                        }
                    }
                    int top = field->m_baseZ + field->m_size - 1;
                    if (first) {
                        minimum = maximum = top;
                        first = false;
                    } else {
                        minimum = (std::min)(minimum, top);
                        maximum = (std::max)(maximum, top);
                    }
                    // The previous height is tested as a boolean.
                    if (dx >= InteriorHeightSampleMargin && dy >= InteriorHeightSampleMargin &&
                        dx < plotSize - InteriorHeightSampleMargin && dy < plotSize - InteriorHeightSampleMargin) {
                        if (interiorTop)
                            interiorTop = top;
                    }
                    // Read twice; nothing depends on the result.
                    if ((field->getMutableBlock(field->m_size - 1).type & ~BlockFlagMask) != BlockGrass)
                        field->getMutableBlock(field->m_size - 1);
                }
            }
            HousePlot &plot = plots[x + y * count];
            plot.minimum = minimum;
            plot.maximum = maximum;
            plot.interiorTop = interiorTop;
            plot.rotation = rand() % 4;
            plot.score = record->influence(
                Fixed(zone->m_position[0] * BlocksPerZone + x * BlocksPerZone / count + plotSize / 2),
                Fixed(zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count + plotSize / 2));
            if (!rejected && hasInfluence) {
                if (hasInteriorRoad) {
                    plot.kind = HousePlotType11Ground;
                } else {
                    plot.kind = plot.score + 0.25f > float(rand()) / RAND_MAX ? HousePlotHouse : HousePlotEmpty;
                    if (maximum - minimum > MaximumPlotRelief)
                        plot.kind = HousePlotEmpty;
                }
            } else {
                plot.kind = HousePlotEmpty;
            }
        }
    }
    // Turn each plot to face a neighbour that is not a house (at most three quarter turns; the
    // rotation is not reduced modulo four).
    for (int x = 0; x < count; ++x) {
        for (int y = 0; y < count; ++y) {
            HousePlot &plot = plots[x + y * count];
            for (int attempt = 0; attempt < 3; ++attempt) {
                int neighborX = x;
                int neighborY = y - 1;
                switch (plot.rotation % 4) {
                case 1:
                    neighborX = x - 1;
                    neighborY = y;
                    break;
                case 2:
                    neighborY = y + 1;
                    break;
                case 3:
                    neighborX = x + 1;
                    neighborY = y;
                    break;
                }
                if (neighborX >= 0 && neighborY >= 0 && neighborX < count && neighborY < count &&
                    plots[neighborX + neighborY * count].kind != HousePlotHouse)
                    break;
                ++plot.rotation;
            }
        }
    }
    if (record->kind == Region::Record::SceneryCity) {
        for (unsigned i = 0; i < plots.size(); ++i) {
            HousePlot &plot = plots[i];
            if (plot.kind == HousePlotEmpty && plot.maximum - plot.minimum < 16) {
                if (rand() % 2 == 0 && plot.score > 0)
                    plot.kind = HousePlotField;
            }
        }
    }
    std::vector<int> eligible;
    std::vector<int> roadPlots;
    for (unsigned i = 0; i < plots.size(); ++i) {
        if (plots[i].kind == HousePlotHouse)
            eligible.push_back(i);
        if (plots[i].kind == HousePlotType11Ground)
            roadPlots.push_back(i);
    }
    if (eligible.empty())
        return false;
    // The houses nearest the record's centre come last.
    std::sort(eligible.begin(), eligible.end(), [&plots](int a, int b) { return plots[a].score < plots[b].score; });

    // The region cell's byte +1 selects the settlement's buildings.
    const int settlementDistrict = zone->cell.style;
    if (record->kind == Region::Record::SceneryCity) {
        if (settlementDistrict == Region::Cell::DistrictTrade && eligible.size() > 1) {
            plots[eligible.back()].kind = HousePlotMarket;
            eligible.pop_back();
        }
        // A city's class halls.
        if (settlementDistrict == Region::Cell::DistrictClass) {
            if (!eligible.empty()) {
                unsigned i = rand() % eligible.size();
                plots[eligible[i]].kind = HousePlotWarriorHall;
                eligible.erase(eligible.begin() + i);
            }
            if (!eligible.empty()) {
                unsigned i = rand() % eligible.size();
                plots[eligible[i]].kind = HousePlotRangerHall;
                eligible.erase(eligible.begin() + i);
            }
            if (!eligible.empty()) {
                unsigned i = rand() % eligible.size();
                plots[eligible[i]].kind = HousePlotRogueHall;
                eligible.erase(eligible.begin() + i);
            }
            if (!eligible.empty()) {
                unsigned i = rand() % eligible.size();
                plots[eligible[i]].kind = HousePlotMageHall;
                eligible.erase(eligible.begin() + i);
            }
        }
        // A city's craft halls.
        if (settlementDistrict == Region::Cell::DistrictCrafting) {
            if (!eligible.empty()) {
                unsigned i = rand() % eligible.size();
                plots[eligible[i]].kind = HousePlotSmithy;
                eligible.erase(eligible.begin() + i);
            }
            if (!eligible.empty()) {
                unsigned i = rand() % eligible.size();
                plots[eligible[i]].kind = HousePlotCarpentersShop;
                eligible.erase(eligible.begin() + i);
            }
            if (!eligible.empty()) {
                unsigned i = rand() % eligible.size();
                plots[eligible[i]].kind = HousePlotWeavingMill;
                eligible.erase(eligible.begin() + i);
            }
        }
        if (settlementDistrict == Region::Cell::DistrictClass && eligible.size() > 1) {
            plots[eligible.back()].kind = HousePlotTower;
            eligible.pop_back();
        }
        if ((settlementDistrict == Region::Cell::DistrictResidential ||
             settlementDistrict == Region::Cell::DistrictClass) &&
            eligible.size() > 1) {
            plots[eligible.back()].variant = HouseVariantInn;
            eligible.pop_back();
        }
        if (settlementDistrict == Region::Cell::DistrictTrade) {
            for (int variant = HouseVariantItemShop; variant < HouseVariantAnalyzer + 1; ++variant) {
                if (!eligible.empty()) {
                    plots[eligible.back()].variant = variant;
                    eligible.pop_back();
                }
            }
        }
        if (settlementDistrict == Region::Cell::DistrictResidential) {
            // No empty-vector check before this modulo.
            unsigned i = rand() % eligible.size();
            plots[eligible[i]].kind = HousePlotWell;
            eligible.erase(eligible.begin() + i);
        }
        for (int k = 0; k < CityPlotRemovalAttempts; ++k) {
            if (eligible.size() > MinimumRemainingHousePlots) {
                unsigned i = rand() % eligible.size();
                plots[eligible[i]].kind = HousePlotEmpty;
                eligible.erase(eligible.begin() + i);
            }
        }
    } else if (record->kind == Region::Record::SceneryRuins) {
        if (zone->m_position[0] == record->center[0].raw / FixedIntegerUnitsPerBlock / BlocksPerZone &&
            zone->m_position[1] == record->center[1].raw / FixedIntegerUnitsPerBlock / BlocksPerZone &&
            eligible.size() > 1) {
            plots[eligible.back()].kind = HousePlotAntiqueLeaderBuilding;
            eligible.pop_back();
        }
        for (int k = 0; k < 2; ++k) {
            if (eligible.size() > 3) {
                unsigned i = rand() % eligible.size();
                plots[eligible[i]].kind = HousePlotEmpty;
                eligible.erase(eligible.begin() + i);
            }
        }
        if (record->style == RuinDesert) {
            // A sphinx and an obelisk, each with a chance of one in two.
            if (rand() % 2 && eligible.size() > 3) {
                unsigned i = rand() % eligible.size();
                HousePlot &plot = plots[eligible[i]];
                eligible.erase(eligible.begin() + i);
                plot.kind = HousePlotSphinx;
                plot.rotation = rand() % 4;
            }
            if (rand() % 2 && eligible.size() > 3) {
                unsigned i = rand() % eligible.size();
                HousePlot &plot = plots[eligible[i]];
                eligible.erase(eligible.begin() + i);
                plot.kind = HousePlotObelisk;
                plot.rotation = rand() % 4;
            }
        } else if (record->style == RuinAntique) {
            for (int k = 0; k < 2; ++k) {
                if (eligible.size() > 3) {
                    unsigned i = rand() % eligible.size();
                    HousePlot &plot = plots[eligible[i]];
                    eligible.erase(eligible.begin() + i);
                    plot.kind = HousePlotAntiqueBuilding;
                    plot.rotation = rand() % 4;
                }
            }
        }
    }

    // The market plazas and the paved squares, places in the villagers' daily walks.
    std::vector<plasma::Vector<3, int>> plazas;
    std::vector<plasma::Vector<3, int>> squares;
    const int halfPlot = plotSize / 2;
    for (int x = 0; x < count; ++x) {
        const int plotX = x * BlocksPerZone / count;
        for (int y = 0; y < count; ++y) {
            HousePlot &plot = plots[x + y * count];
            const int plotY = y * BlocksPerZone / count;

            // A stepped white pyramid with a tall object on its top.
            if (plot.kind == HousePlotPyramid) {
                const int centerX = zone->m_position[0] * BlocksPerZone + plotX + halfPlot;
                const int centerY = zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count + halfPlot;
                Field *field = getField(centerX, centerY, zone);
                // Pyramid layout: four-block rings below width 24, pale stone and a 3 by 3 by 12 statue.
                const int top = field->m_baseZ + field->m_size + 1;
                for (int step = 4; step < 24; step += 4) {
                    fillBox(centerX - step / 2, centerY - step / 2, top - step / 4 + 1,
                            plasma::Vector<3, int>(step, step, 1), plasma::Vector<3, unsigned char>(220, 220, 220),
                            0.0f, zone, 0);
                }
                Zone::Object object;
                object.position = plasma::Vector<3, Fixed>(Fixed(centerX), Fixed(centerY), Fixed(top));
                object.size = plasma::Vector<3, float>(3.0f, 3.0f, 12.0f);
                object.type = StaticObjectStatue;
                // Raise it out of the pyramid's blocks.
                for (;;) {
                    int type = getBlock(object.position[0], object.position[1], object.position[2], zone).type &
                               ~BlockFlagMask;
                    if (type == BlockAir || type == BlockWater)
                        break;
                    object.position[2] = object.position[2] + 1;
                }
                object.rotation = rand() % 4;
                zone->m_objects.push_back(object);
            }

            // The market: twelve stands on the four sides of the plaza and eight small stalls, each with a
            // chance of four in five.
            if (plot.kind == HousePlotMarket) {
                const int originX = zone->m_position[0] * BlocksPerZone + plotX;
                const int originY = zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count;
                const int centerX = originX + halfPlot;
                const int centerY = originY + halfPlot;
                Field *field = getField(centerX, centerY, zone);
                if (field)
                    plazas.push_back(plasma::Vector<3, int>(centerX, centerY, field->m_baseZ + field->m_size));
                const int z = plot.maximum;
                // Large market layout: stand spacing, edge inset and placement jitter.
                if (rand() % 5) {
                    Zone::Object stand =
                        makeLargeHouseObject(plasma::Vector<3, Fixed>(centerX - 7, originY + 6 + rand() % 3, z), 2, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand =
                        makeLargeHouseObject(plasma::Vector<3, Fixed>(centerX, originY + 6 + rand() % 3, z), 2, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand =
                        makeLargeHouseObject(plasma::Vector<3, Fixed>(centerX + 7, originY + 6 + rand() % 3, z), 2, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeLargeHouseObject(
                        plasma::Vector<3, Fixed>(centerX - 7, originY + plotSize - 6 - rand() % 3, z), 0, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeLargeHouseObject(
                        plasma::Vector<3, Fixed>(centerX, originY + plotSize - 6 - rand() % 3, z), 0, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeLargeHouseObject(
                        plasma::Vector<3, Fixed>(centerX + 7, originY + plotSize - 6 - rand() % 3, z), 0, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeLargeHouseObject(
                        plasma::Vector<3, Fixed>(originX + plotSize - 6 - rand() % 3, centerY - 7, z), 3, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeLargeHouseObject(
                        plasma::Vector<3, Fixed>(originX + plotSize - 6 - rand() % 3, centerY, z), 3, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeLargeHouseObject(
                        plasma::Vector<3, Fixed>(originX + plotSize - 6 - rand() % 3, centerY + 7, z), 3, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand =
                        makeLargeHouseObject(plasma::Vector<3, Fixed>(originX + 6 + rand() % 3, centerY - 7, z), 1, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand =
                        makeLargeHouseObject(plasma::Vector<3, Fixed>(originX + 6 + rand() % 3, centerY, z), 1, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand =
                        makeLargeHouseObject(plasma::Vector<3, Fixed>(originX + 6 + rand() % 3, centerY + 7, z), 1, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand =
                        makeSmallHouseObject(plasma::Vector<3, Fixed>(centerX - 3, originY + 6 + rand() % 3, z), 2, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand =
                        makeSmallHouseObject(plasma::Vector<3, Fixed>(centerX + 3, originY + 6 + rand() % 3, z), 2, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeSmallHouseObject(
                        plasma::Vector<3, Fixed>(centerX - 3, originY + plotSize - 6 - rand() % 3, z), 0, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeSmallHouseObject(
                        plasma::Vector<3, Fixed>(centerX + 3, originY + plotSize - 6 - rand() % 3, z), 0, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeSmallHouseObject(
                        plasma::Vector<3, Fixed>(originX + plotSize - 6 - rand() % 3, centerY - 3, z), 3, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand = makeSmallHouseObject(
                        plasma::Vector<3, Fixed>(originX + plotSize - 6 - rand() % 3, centerY + 3, z), 3, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand =
                        makeSmallHouseObject(plasma::Vector<3, Fixed>(originX + 6 + rand() % 3, centerY - 3, z), 1, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
                if (rand() % 5) {
                    Zone::Object stand =
                        makeSmallHouseObject(plasma::Vector<3, Fixed>(originX + 6 + rand() % 3, centerY + 3, z), 1, 0);
                    if (settleObject(stand, zone, true))
                        zone->m_objects.push_back(stand);
                }
            }

            // A field of one crop: furrowed farmland, a scarecrow in the middle, the crop's plants or its
            // produce lying about, and two rows of fence posts.
            if (plot.kind == HousePlotField) {
                const int furrowsAlongX = rand() % 2;
                int produce = 0;
                int plantType = 0;
                // Crop model scales by planted species.
                unsigned int plantFlags = 0;
                float plantScale = 0.1f;
                const int crop = rand() % FarmCropCount;
                for (int i = 0; i < plotSize; ++i) {
                    for (int j = 0; j < plotSize; ++j) {
                        switch (crop) {
                        case FarmCropPumpkin:
                            produce = MaterialItemPumpkin;
                            plantType = PlantPumpkinLeaves;
                            break;
                        case FarmCropSunflower:
                            plantScale = 0.2f;
                            plantType = PlantSunflower;
                            plantFlags = (1 << PlantFlagSway);
                            break;
                        case FarmCropBean:
                            plantScale = 0.2f;
                            plantType = PlantBeanTendril;
                            break;
                        case FarmCropWheat:
                            plantFlags = (1 << PlantFlagSway);
                            if (rand() % CornflowerChanceDenominator == 0) {
                                plantScale = 0.05f;
                                plantType = PlantCornflower;
                            } else {
                                plantScale = 0.2f;
                                plantType = PlantWheat;
                            }
                            break;
                        case FarmCropCorn:
                            plantScale = 0.2f;
                            plantType = PlantCorn;
                            plantFlags = (1 << PlantFlagSway);
                            break;
                        case FarmCropPineapple:
                            plantScale = 0.15f;
                            produce = MaterialItemPineapple;
                            plantType = PlantPineappleLeaves;
                            break;
                        }
                        const int blockX = zone->m_position[0] * BlocksPerZone + plotX + i;
                        const int blockY = zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count + j;
                        Field *field = getField(blockX, blockY, zone);
                        if (!field->size())
                            continue;
                        if (field->getMutableBlock(field->size() - 1).blockType() != BlockGrass)
                            continue;
                        // Every other pair of rows is lighter.
                        plasma::Vector<3, float> stripe;
                        if (furrowsAlongX)
                            stripe = plasma::Vector<3, float>(10.0f, 10.0f, 0.0f) * float(j / 2 % 2);
                        else
                            stripe = plasma::Vector<3, float>(10.0f, 10.0f, 0.0f) * float(i / 2 % 2);
                        plasma::Vector<3, unsigned char> color(clampColor(
                            groundColor(blockX, blockY, field->getBaseZ() - 1 + field->size(), zone) * 0.7f + stripe));
                        field->setBlock(field->size() - 1, Block(color, BlockGround));
                        if (i == halfPlot && j == halfPlot) {
                            Spawn *scarecrow = new Spawn;
                            scarecrow->hostilityType = HostilityAttackableObject;
                            scarecrow->race = RaceScarecrow;
                            scarecrow->level = levelAt(Fixed(blockX), Fixed(blockY));
                            scarecrow->position = plasma::Vector<3, Fixed>(Fixed(blockX) + 0.5, Fixed(blockY) + 0.5,
                                                                           Fixed(field->getBaseZ() + field->size()));
                            scarecrow->rotation = rand() % 4 * 90.0f;
                            zone->m_spawns.push_back(scarecrow);
                        } else if (rand() % CropPlacementChanceDenominator == 0) {
                            if (rand() % ProduceChanceDenominator == 0 && produce) {
                                Zone::ZoneItem item;
                                item.item.category = ItemCategoryMaterial;
                                item.item.id = produce;
                                item.position = plasma::Vector<3, Fixed>(Fixed(blockX), Fixed(blockY),
                                                                         Fixed(field->getBaseZ() + field->size())) +
                                                plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.5f, 0.5f, 0.0f));
                                item.rotation = rand() * 360.0f / RAND_MAX;
                                item.scale = 0.1f;
                                item.flags = (1 << ZoneItemFlagGeneratedMaterial);
                                zone->m_items.push_back(item);
                            } else if (plantType) {
                                Zone::Plant plant;
                                plant.type = plantType;
                                plant.flags = plantFlags;
                                if (plantType == PlantSunflower)
                                    plant.rotation = 0.0f;
                                else
                                    plant.rotation = rand() * 360.0f / RAND_MAX;
                                plant.position = plasma::Vector<3, Fixed>(Fixed(blockX), Fixed(blockY),
                                                                          Fixed(field->getBaseZ() + field->size())) +
                                                 plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.5f, 0.5f, 0.0f));
                                plant.scale = plantScale;
                                zone->m_plants.push_back(plant);
                            }
                        }
                    }
                }
                // Fence posts along the plot's lower x and y edges.
                if (rand() % 2) {
                    for (int i = 0; i < halfPlot; ++i) {
                        if (rand() % FencePostOmissionDenominator == 0)
                            continue;
                        const int blockX = zone->m_position[0] * BlocksPerZone + plotX + i * 2 + 1;
                        const int blockY = zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count;
                        Field *field = getField(blockX, blockY, zone);
                        if (!field->size() || !field->getMutableBlock(field->size() - 1).blockType())
                            continue;
                        const int z = field->getBaseZ() + field->size();
                        Zone::Object post;
                        post.type = StaticObjectFence1 + rand() % FenceVariantCount;
                        post.position = plasma::Vector<3, Fixed>(Fixed(blockX), Fixed(blockY), Fixed(z)) +
                                        plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.5f, 0.0f));
                        // Fence model dimensions by side orientation.
                        post.rotation = 0;
                        post.size = plasma::Vector<3, float>(2.0f, 1.0f, 1.5f);
                        zone->m_objects.push_back(post);
                    }
                }
                if (rand() % 2) {
                    for (int i = 0; i < halfPlot; ++i) {
                        if (rand() % 10 == 0)
                            continue;
                        const int blockX = zone->m_position[0] * BlocksPerZone + plotX;
                        const int blockY = zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count + i * 2 + 1;
                        Field *field = getField(blockX, blockY, zone);
                        if (!field->size() || !field->getMutableBlock(field->size() - 1).blockType())
                            continue;
                        const int z = field->getBaseZ() + field->size();
                        Zone::Object post;
                        post.type = StaticObjectFence1 + rand() % 4;
                        // The same offset as the other row.
                        post.position = plasma::Vector<3, Fixed>(Fixed(blockX), Fixed(blockY), Fixed(z)) +
                                        plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.5f, 0.0f));
                        post.rotation = 1;
                        post.size = plasma::Vector<3, float>(2.0f, 1.0f, 1.5f);
                        zone->m_objects.push_back(post);
                    }
                }
            }

            // Sampled at every plot's centre; the results are not used.
            blendedHumidity(zone->m_position[0] * BlocksPerZone + halfPlot + plotX,
                            zone->m_position[1] * BlocksPerZone + halfPlot + y * BlocksPerZone / count);
            blendedTemperature(zone->m_position[0] * BlocksPerZone + halfPlot + plotX,
                               zone->m_position[1] * BlocksPerZone + halfPlot + y * BlocksPerZone / count);

            if (plot.kind == HousePlotHouse) {
                // The building kit: the record's mode and subtype choose the material; ruins repeat their plain
                // wall for the openings.
                Sprite *entranceStairs = m_spriteManager.sprite(SpriteEntranceStairs);
                Sprite *base, *floor, *floorStairs, *wall, *wallWindow, *wallDoor, *wallIndoor, *wallBalcony, *wallLamp;
                Sprite *baseRoofHalf, *perpendicularJoinRoofHalf, *parallelJoinRoofHalf, *arc;
                if (record->kind == Region::Record::SceneryCity) {
                    switch (record->style) {
                    case CityWood:
                        base = m_spriteManager.sprite(SpriteWoodBase);
                        floor = m_spriteManager.sprite(SpriteWoodFloor);
                        floorStairs = m_spriteManager.sprite(SpriteWoodFloorStairs);
                        wall = m_spriteManager.sprite(SpriteWoodWall);
                        wallWindow = m_spriteManager.sprite(SpriteWoodWallWindow);
                        wallDoor = m_spriteManager.sprite(SpriteWoodWallDoor);
                        wallIndoor = m_spriteManager.sprite(SpriteWoodWallIndoor);
                        wallBalcony = m_spriteManager.sprite(SpriteWoodWallBalcony);
                        wallLamp = m_spriteManager.sprite(SpriteWoodWallLamp);
                        baseRoofHalf = m_spriteManager.sprite(SpriteWoodRoof1);
                        perpendicularJoinRoofHalf = m_spriteManager.sprite(SpriteWoodRoof2);
                        parallelJoinRoofHalf = m_spriteManager.sprite(SpriteWoodRoof3);
                        arc = m_spriteManager.sprite(SpriteWoodArc);
                        break;
                    case CityWhitewood:
                        base = m_spriteManager.sprite(SpriteWhitewoodBase);
                        floor = m_spriteManager.sprite(SpriteWhitewoodFloor);
                        floorStairs = m_spriteManager.sprite(SpriteWhitewoodFloorStairs);
                        wall = m_spriteManager.sprite(SpriteWhitewoodWall);
                        wallWindow = m_spriteManager.sprite(SpriteWhitewoodWallWindow);
                        wallDoor = m_spriteManager.sprite(SpriteWhitewoodWallDoor);
                        wallIndoor = m_spriteManager.sprite(SpriteWhitewoodWallIndoor);
                        wallBalcony = m_spriteManager.sprite(SpriteWhitewoodWallBalcony);
                        wallLamp = m_spriteManager.sprite(SpriteWhitewoodWallLamp);
                        baseRoofHalf = m_spriteManager.sprite(SpriteWhitewoodRoof1);
                        perpendicularJoinRoofHalf = m_spriteManager.sprite(SpriteWhitewoodRoof2);
                        parallelJoinRoofHalf = m_spriteManager.sprite(SpriteWhitewoodRoof3);
                        arc = m_spriteManager.sprite(SpriteWhitewoodArc);
                        break;
                    case CityStone:
                        base = m_spriteManager.sprite(SpriteStoneBase);
                        floor = m_spriteManager.sprite(SpriteStoneFloor);
                        floorStairs = m_spriteManager.sprite(SpriteStoneFloorStairs);
                        wall = m_spriteManager.sprite(SpriteStoneWall);
                        wallWindow = m_spriteManager.sprite(SpriteStoneWallWindow);
                        wallDoor = m_spriteManager.sprite(SpriteStoneWallDoor);
                        wallIndoor = m_spriteManager.sprite(SpriteStoneWallIndoor);
                        wallBalcony = m_spriteManager.sprite(SpriteStoneWallBalcony);
                        wallLamp = m_spriteManager.sprite(SpriteStoneWallLamp);
                        baseRoofHalf = m_spriteManager.sprite(SpriteStoneRoof1);
                        perpendicularJoinRoofHalf = m_spriteManager.sprite(SpriteStoneRoof2);
                        parallelJoinRoofHalf = m_spriteManager.sprite(SpriteStoneRoof3);
                        arc = m_spriteManager.sprite(SpriteStoneArc);
                        break;
                    case CityClay:
                        entranceStairs = m_spriteManager.sprite(SpriteClayEntranceStairs);
                        base = m_spriteManager.sprite(SpriteClayBase);
                        floor = m_spriteManager.sprite(SpriteClayFloor);
                        floorStairs = m_spriteManager.sprite(SpriteClayFloorStairs);
                        wall = m_spriteManager.sprite(SpriteClayWall);
                        wallWindow = m_spriteManager.sprite(SpriteClayWallWindow);
                        wallDoor = m_spriteManager.sprite(SpriteClayWallDoor);
                        wallIndoor = m_spriteManager.sprite(SpriteClayWallIndoor);
                        wallBalcony = m_spriteManager.sprite(SpriteClayWallBalcony);
                        wallLamp = m_spriteManager.sprite(SpriteClayWallLamp);
                        baseRoofHalf = m_spriteManager.sprite(SpriteClayRoof1);
                        perpendicularJoinRoofHalf = m_spriteManager.sprite(SpriteClayRoof2);
                        parallelJoinRoofHalf = m_spriteManager.sprite(SpriteClayRoof3);
                        arc = m_spriteManager.sprite(SpriteClayArc);
                        break;
                    case CityWhiteclay:
                        entranceStairs = m_spriteManager.sprite(SpriteWhiteclayEntranceStairs);
                        base = m_spriteManager.sprite(SpriteWhiteclayBase);
                        floor = m_spriteManager.sprite(SpriteWhiteclayFloor);
                        floorStairs = m_spriteManager.sprite(SpriteWhiteclayFloorStairs);
                        wall = m_spriteManager.sprite(SpriteWhiteclayWall);
                        wallWindow = m_spriteManager.sprite(SpriteWhiteclayWallWindow);
                        wallDoor = m_spriteManager.sprite(SpriteWhiteclayWallDoor);
                        wallIndoor = m_spriteManager.sprite(SpriteWhiteclayWallIndoor);
                        wallBalcony = m_spriteManager.sprite(SpriteWhiteclayWallBalcony);
                        wallLamp = m_spriteManager.sprite(SpriteWhiteclayWallLamp);
                        baseRoofHalf = m_spriteManager.sprite(SpriteWhiteclayRoof1);
                        perpendicularJoinRoofHalf = m_spriteManager.sprite(SpriteWhiteclayRoof2);
                        parallelJoinRoofHalf = m_spriteManager.sprite(SpriteWhiteclayRoof3);
                        arc = m_spriteManager.sprite(SpriteWhiteclayArc);
                        break;
                    default:
                        base = m_spriteManager.sprite(SpriteFrameworkBase);
                        floor = m_spriteManager.sprite(SpriteFrameworkFloor);
                        floorStairs = m_spriteManager.sprite(SpriteFrameworkFloorStairs);
                        wall = m_spriteManager.sprite(SpriteFrameworkWall);
                        wallWindow = m_spriteManager.sprite(SpriteFrameworkWallWindow);
                        wallDoor = m_spriteManager.sprite(SpriteFrameworkWallDoor);
                        wallIndoor = m_spriteManager.sprite(SpriteFrameworkWallIndoor);
                        wallBalcony = m_spriteManager.sprite(SpriteFrameworkWallBalcony);
                        wallLamp = m_spriteManager.sprite(SpriteFrameworkWallLamp);
                        baseRoofHalf = m_spriteManager.sprite(SpriteFrameworkRoof1);
                        perpendicularJoinRoofHalf = m_spriteManager.sprite(SpriteFrameworkRoof2);
                        parallelJoinRoofHalf = m_spriteManager.sprite(SpriteFrameworkRoof3);
                        arc = m_spriteManager.sprite(SpriteFrameworkArc);
                        break;
                    }
                } else {
                    switch (record->style) {
                    case RuinDesert:
                        entranceStairs = m_spriteManager.sprite(SpriteDesertruinEntranceStairs);
                        base = m_spriteManager.sprite(SpriteDesertruinBase);
                        floor = m_spriteManager.sprite(SpriteDesertruinFloor);
                        floorStairs = m_spriteManager.sprite(SpriteDesertruinFloorStairs);
                        wall = m_spriteManager.sprite(SpriteDesertruinWall);
                        wallWindow = m_spriteManager.sprite(SpriteDesertruinWall);
                        wallDoor = m_spriteManager.sprite(SpriteDesertruinWallDoor);
                        wallIndoor = m_spriteManager.sprite(SpriteDesertruinWallIndoor);
                        wallBalcony = m_spriteManager.sprite(SpriteDesertruinWallDoor);
                        wallLamp = m_spriteManager.sprite(SpriteDesertruinWallDoor);
                        baseRoofHalf = m_spriteManager.sprite(SpriteDesertruinRoof1);
                        perpendicularJoinRoofHalf = m_spriteManager.sprite(SpriteDesertruinRoof2);
                        parallelJoinRoofHalf = m_spriteManager.sprite(SpriteDesertruinRoof3);
                        arc = m_spriteManager.sprite(SpriteDesertruinArc);
                        break;
                    case RuinJungle:
                        entranceStairs = m_spriteManager.sprite(SpriteJungleruinEntranceStairs);
                        base = m_spriteManager.sprite(SpriteJungleruinBase);
                        floor = m_spriteManager.sprite(SpriteJungleruinFloor);
                        floorStairs = m_spriteManager.sprite(SpriteJungleruinFloorStairs);
                        wall = m_spriteManager.sprite(SpriteJungleruinWall);
                        wallWindow = m_spriteManager.sprite(SpriteJungleruinWall);
                        wallDoor = m_spriteManager.sprite(SpriteJungleruinWallDoor);
                        wallIndoor = m_spriteManager.sprite(SpriteJungleruinWallIndoor);
                        wallBalcony = m_spriteManager.sprite(SpriteJungleruinWall);
                        wallLamp = m_spriteManager.sprite(SpriteJungleruinWall);
                        baseRoofHalf = m_spriteManager.sprite(SpriteJungleruinRoof1);
                        perpendicularJoinRoofHalf = m_spriteManager.sprite(SpriteJungleruinRoof2);
                        parallelJoinRoofHalf = m_spriteManager.sprite(SpriteJungleruinRoof3);
                        arc = m_spriteManager.sprite(SpriteJungleruinArc);
                        break;
                    default:
                        entranceStairs = m_spriteManager.sprite(SpriteAntiqueruinEntranceStairs);
                        base = m_spriteManager.sprite(SpriteAntiqueruinBase);
                        floor = m_spriteManager.sprite(SpriteAntiqueruinFloor);
                        floorStairs = m_spriteManager.sprite(SpriteAntiqueruinFloorStairs);
                        wall = m_spriteManager.sprite(SpriteAntiqueruinWall);
                        wallWindow = m_spriteManager.sprite(SpriteAntiqueruinWall);
                        wallDoor = m_spriteManager.sprite(SpriteAntiqueruinWall);
                        wallIndoor = m_spriteManager.sprite(SpriteAntiqueruinWall);
                        wallBalcony = m_spriteManager.sprite(SpriteAntiqueruinWall);
                        wallLamp = m_spriteManager.sprite(SpriteAntiqueruinWall);
                        baseRoofHalf = m_spriteManager.sprite(SpriteAntiqueruinRoof1);
                        perpendicularJoinRoofHalf = m_spriteManager.sprite(SpriteAntiqueruinRoof2);
                        parallelJoinRoofHalf = m_spriteManager.sprite(SpriteAntiqueruinRoof3);
                        arc = m_spriteManager.sprite(SpriteAntiqueruinArc);
                        break;
                    }
                }

                // House floor-plan lattice: three cells across, three deep, four storeys.
                House *house = new House(3, 3, 4);
                // The objects added from here on belong to this house.
                const int firstObject = (int)zone->m_objects.size();
                house->variant = plot.variant;
                // The house's layout cells.
                if (plot.variant == HouseVariantItemShop || plot.variant == HouseVariantWeaponShop ||
                    plot.variant == HouseVariantArmorShop || plot.variant == HouseVariantAnalyzer) {
                    house->cell(0, 0, 0).kind = HouseCellBase;
                    house->cell(0, 1, 0).kind = HouseCellBase;
                    house->cell(1, 1, 0).kind = HouseCellBase;
                    house->cell(1, 0, 0).kind = HouseCellBase;
                    house->cell(0, 0, 1).kind = HouseCellFloor;
                    house->cell(0, 1, 1).kind = HouseCellFloor;
                    house->cell(0, 1, 1).variant = HouseCellCounter;
                    house->cell(1, 1, 1).kind = HouseCellFloor;
                    house->cell(1, 0, 1).kind = HouseCellFloor;
                    house->cell(0, 0, 2).kind = HouseCellRoof;
                    house->cell(0, 1, 2).kind = HouseCellRoof;
                    house->cell(1, 1, 2).kind = HouseCellRoof;
                    house->cell(1, 0, 2).kind = HouseCellRoof;
                } else if (plot.variant == HouseVariantInn) {
                    house->cell(0, 0, 0).kind = HouseCellBase;
                    house->cell(0, 0, 1).kind = HouseCellFloor;
                    house->cell(0, 0, 1).variant = HouseCellDining;
                    house->cell(0, 0, 2).kind = HouseCellFloor;
                    house->cell(0, 0, 2).stairs = true;
                    house->cell(0, 0, 3).kind = HouseCellRoof;
                    house->cell(1, 0, 0).kind = HouseCellBase;
                    house->cell(1, 0, 1).kind = HouseCellFloor;
                    house->cell(1, 0, 1).variant = HouseCellDining;
                    house->cell(1, 0, 2).kind = HouseCellFloor;
                    house->cell(1, 0, 2).variant = HouseCellBedroom;
                    house->cell(1, 0, 3).kind = HouseCellRoof;
                    house->cell(2, 0, 0).kind = HouseCellBase;
                    house->cell(2, 0, 1).kind = HouseCellFloor;
                    house->cell(2, 0, 1).variant = HouseCellDining;
                    house->cell(2, 0, 2).kind = HouseCellFloor;
                    house->cell(2, 0, 2).variant = HouseCellBedroom;
                    house->cell(2, 0, 3).kind = HouseCellRoof;
                    house->cell(0, 1, 0).kind = HouseCellBase;
                    house->cell(0, 1, 1).kind = HouseCellFloor;
                    house->cell(0, 1, 1).variant = HouseCellDining;
                    house->cell(0, 1, 2).kind = HouseCellRoof;
                    if (rand() % 2) {
                        house->cell(2, 1, 0).kind = HouseCellBase;
                        house->cell(2, 1, 1).kind = HouseCellFloor;
                        house->cell(2, 1, 1).variant = HouseCellDining;
                        house->cell(2, 1, 2).kind = HouseCellRoof;
                    }
                    house->cell(1, 1, 0).kind = HouseCellBase;
                    house->cell(1, 1, 1).kind = HouseCellFloor;
                    house->cell(1, 1, 1).variant = HouseCellCounter;
                    house->cell(1, 1, 2).kind = HouseCellRoof;
                    house->cell(1, 2, 0).kind = HouseCellBase;
                    house->cell(1, 2, 1).kind = HouseCellFloor;
                    house->cell(1, 2, 2).kind = HouseCellRoof;
                } else {
                    switch (rand() % 3) {
                    case 2:
                        if (rand() % 2) {
                            house->cell(0, 0, 0).kind = HouseCellBase;
                            house->cell(0, 0, 1).kind = HouseCellFloor;
                            house->cell(0, 0, 2).kind = HouseCellRoof;
                            house->cell(1, 0, 0).kind = HouseCellBase;
                            house->cell(1, 0, 1).kind = HouseCellFloor;
                            house->cell(1, 0, 1).variant = HouseCellBedroom;
                            house->cell(1, 0, 2).kind = HouseCellRoof;
                            house->cell(2, 0, 0).kind = HouseCellBase;
                            house->cell(2, 0, 1).kind = HouseCellFloor;
                            house->cell(2, 0, 2).kind = HouseCellRoof;
                        } else {
                            house->cell(0, 0, 0).kind = HouseCellBase;
                            house->cell(0, 0, 1).kind = HouseCellFloor;
                            house->cell(0, 0, 2).kind = HouseCellFloor;
                            house->cell(0, 0, 2).stairs = true;
                            house->cell(0, 0, 3).kind = HouseCellRoof;
                            house->cell(1, 0, 0).kind = HouseCellBase;
                            house->cell(1, 0, 1).kind = HouseCellFloor;
                            house->cell(1, 0, 2).kind = HouseCellFloor;
                            house->cell(1, 0, 3).kind = HouseCellRoof;
                            house->cell(2, 0, 0).kind = HouseCellBase;
                            house->cell(2, 0, 1).kind = HouseCellFloor;
                            house->cell(2, 0, 1).variant = HouseCellBedroom;
                            house->cell(2, 0, 2).kind = HouseCellFloor;
                            house->cell(2, 0, 3).kind = HouseCellRoof;
                        }
                        if (rand() % 2) {
                            house->cell(0, 1, 0).kind = HouseCellBase;
                            house->cell(0, 1, 1).kind = HouseCellFloor;
                            house->cell(0, 1, 2).kind = HouseCellRoof;
                        }
                        if (rand() % 2) {
                            house->cell(2, 1, 0).kind = HouseCellBase;
                            house->cell(2, 1, 1).kind = HouseCellFloor;
                            house->cell(2, 1, 2).kind = HouseCellRoof;
                        }
                        house->cell(1, 1, 0).kind = HouseCellBase;
                        house->cell(1, 1, 1).kind = HouseCellFloor;
                        house->cell(1, 1, 2).kind = HouseCellRoof;
                        break;
                    case 1:
                        house->cell(0, 0, 0).kind = HouseCellBase;
                        house->cell(0, 0, 1).kind = HouseCellFloor;
                        house->cell(0, 0, 2).kind = HouseCellRoof;
                        house->cell(1, 0, 0).kind = HouseCellBase;
                        house->cell(1, 0, 1).kind = HouseCellFloor;
                        house->cell(1, 0, 2).kind = HouseCellRoof;
                        house->cell(0, 1, 0).kind = HouseCellBase;
                        house->cell(0, 1, 1).kind = HouseCellFloor;
                        house->cell(0, 1, 1).variant = HouseCellBedroom;
                        house->cell(0, 1, 2).kind = HouseCellRoof;
                        house->cell(1, 1, 0).kind = HouseCellBase;
                        house->cell(1, 1, 1).kind = HouseCellFloor;
                        if (rand() % 2) {
                            house->cell(1, 1, 2).kind = HouseCellFloor;
                            house->cell(1, 1, 2).stairs = true;
                            house->cell(1, 1, 3).kind = HouseCellRoof;
                        } else {
                            house->cell(1, 1, 2).kind = HouseCellRoof;
                        }
                        if (rand() % 2) {
                            house->cell(0, 2, 0).kind = HouseCellBase;
                            house->cell(0, 2, 1).kind = HouseCellFloor;
                            house->cell(0, 2, 2).kind = HouseCellRoof;
                        }
                        break;
                    case 0:
                        house->cell(0, 0, 0).kind = HouseCellBase;
                        house->cell(0, 0, 1).kind = HouseCellFloor;
                        house->cell(0, 0, 2).kind = HouseCellFloor;
                        house->cell(0, 0, 2).variant = HouseCellBedroom;
                        house->cell(0, 0, 2).stairs = true;
                        house->cell(0, 0, 3).kind = HouseCellRoof;
                        house->cell(2, 0, 0).kind = HouseCellBase;
                        house->cell(2, 0, 1).kind = HouseCellFloor;
                        house->cell(2, 0, 2).kind = HouseCellFloor;
                        house->cell(2, 0, 2).stairs = true;
                        house->cell(2, 0, 3).kind = HouseCellRoof;
                        house->cell(0, 1, 0).kind = HouseCellBase;
                        house->cell(0, 1, 1).kind = HouseCellFloor;
                        if (rand() % 2 == 0) {
                            house->cell(0, 1, 2).kind = HouseCellFloor;
                            house->cell(0, 1, 3).kind = HouseCellRoof;
                        } else {
                            house->cell(0, 1, 2).kind = HouseCellRoof;
                        }
                        if (rand() % 2) {
                            house->cell(2, 1, 0).kind = HouseCellBase;
                            house->cell(2, 1, 1).kind = HouseCellFloor;
                            house->cell(2, 1, 2).kind = HouseCellRoof;
                        }
                        if (rand() % 2) {
                            house->cell(1, 0, 1).kind = HouseCellArc;
                        } else {
                            house->cell(1, 0, 0).kind = HouseCellBase;
                            house->cell(1, 0, 1).kind = HouseCellFloor;
                            house->cell(1, 0, 2).kind = HouseCellRoof;
                        }
                        break;
                    }
                }
                house->m_rotation = plot.rotation;
                house->m_reflectY = rand() % 2 == 0;

                // Stand it on the highest ground under its upper floor.
                std::vector<plasma::Vector<3, int>> highestCells;
                int highest = 0;
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        if (house->cell(x, y, 1).kind != HouseCellFloor || house->cell(x, y, 2).stairs ||
                            house->cell(x, y, 1).variant == HouseCellCounter)
                            continue;
                        int top = 0;
                        for (int dx = 0; dx < HouseCellWidth; ++dx) {
                            for (int dy = 0; dy < 13; ++dy) {
                                Field *field = getField(zone->m_position[0] * BlocksPerZone + plotX +
                                                            x * HouseCellWidth + HouseCellCenterOffset + dx,
                                                        zone->m_position[1] * BlocksPerZone + plotY +
                                                            y * HouseCellWidth + HouseCellCenterOffset + dy,
                                                        zone);
                                if (field && field->getBaseZ() + field->size() > top)
                                    top = field->getBaseZ() + field->size();
                            }
                        }
                        house->cell(x, y, 1).groundHeight = top;
                        highest = (std::max)(highest, top);
                    }
                }
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        if (house->cell(x, y, 1).kind == HouseCellFloor && !house->cell(x, y, 2).stairs &&
                            house->cell(x, y, 1).groundHeight == highest &&
                            house->cell(x, y, 1).variant != HouseCellCounter)
                            highestCells.push_back(plasma::Vector<3, int>(x, y, 1));
                    }
                }
                if (!highestCells.empty()) {
                    plasma::Vector<3, int> selected = highestCells[rand() % highestCells.size()];
                    house->cell(selected[0], selected[1], selected[2]).selected = true;
                }
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        for (int z = 0; z < house->sizeZ(); ++z) {
                            if (house->cell(x, y, z).kind == HouseCellArc)
                                house->cell(x, y, z).rotation = plot.rotation % 2;
                            else
                                house->cell(x, y, z).rotation = rand() % 4;
                        }
                    }
                }
                int heightOffset = 0;
                bool first = true;
                for (int z = house->sizeZ() - 1; z >= 0; --z) {
                    for (int x = 0; x < house->sizeX(); ++x) {
                        for (int y = 0; y < house->sizeY(); ++y) {
                            if (!house->cell(x, y, z).selected)
                                // Entrance footprint: expanded cell extent and inset before testing ground.
                                continue;
                            for (int dx = 0; dx < 15; ++dx) {
                                for (int dy = 0; dy < 15; ++dy) {
                                    Field *field = getField(
                                        zone->m_position[0] * BlocksPerZone + plotX + x * HouseCellWidth + 6 + dx,
                                        zone->m_position[1] * BlocksPerZone + plotY + y * HouseCellWidth + 6 + dy,
                                        zone);
                                    if (field) {
                                        int difference = field->getBaseZ() - highest + field->size();
                                        if (first || difference > heightOffset) {
                                            heightOffset = difference;
                                            first = false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                house->m_position = plasma::Vector<3, int>(zone->m_position[0] * BlocksPerZone + plotX + 7,
                                                           zone->m_position[1] * BlocksPerZone + plotY + 7,
                                                           highest - HouseFoundationDepth + heightOffset);
                zone->m_houses.push_back(house);

                // Raise type-11 ground around it to the house's floor.
                const int minimumX = house->m_position[0] - HouseRoadBorderWidth;
                const int minimumY = house->m_position[1] - HouseRoadBorderWidth;
                int maximumX = minimumX;
                int maximumY = minimumY;
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        if (house->cell(x, y, 0).kind) {
                            maximumX = (std::max)(maximumX, house->m_position[0] + x * HouseCellWidth + HouseCellWidth);
                            maximumY = (std::max)(maximumY, house->m_position[1] + y * HouseCellWidth + HouseCellWidth);
                        }
                    }
                }
                maximumX += 2;
                maximumY += 2;
                if (record->kind == Region::Record::SceneryCity && record->style != CityClay &&
                    record->style != CityWhiteclay) {
                    // Check the entire footprint before extending any of its columns: every ground block must be type
                    // 11 and not below the house's floor.
                    bool fits = true;
                    for (int x = minimumX; fits && x <= maximumX; ++x) {
                        for (int y = minimumY; fits && y <= maximumY; ++y) {
                            Field *field = getField(x, y, zone);
                            if (!field)
                                continue;
                            int z = field->getBaseZ() - 1 + field->size();
                            while (!getBlock(x, y, z, zone).isSolid())
                                --z;
                            if (getBlock(x, y, z, zone).blockType() != BlockRoad || 5 - z + house->m_position[2] > 5)
                                fits = false;
                        }
                    }
                    // Raise the ground to the floor: the rim in grey stone, the inside in the ground's own colour.
                    for (int x = minimumX; fits && x <= maximumX; ++x) {
                        for (int y = minimumY; y <= maximumY; ++y) {
                            Field *field = getField(x, y, zone);
                            if (!field)
                                continue;
                            int z = field->getBaseZ() - 1 + field->size();
                            while (!getBlock(x, y, z, zone).isSolid())
                                --z;
                            const Block &ground = getBlock(x, y, z, zone);
                            if (ground.blockType() != BlockRoad)
                                continue;
                            plasma::Vector<3, float> color(ground.color);
                            // Foundation-rim stone palette: independent noise channel offsets and brightness variation.
                            if (x == minimumX || x == maximumX || y == minimumY || y == maximumY) {
                                color = plasma::Vector<3, float>(140.0f, 140.0f, 140.0f) +
                                        noise(x * 0.05 + 843.0, y * 0.05 + 984.0) *
                                            plasma::Vector<3, float>(40.0f, 40.0f, 40.0f);
                                color *= 0.7f;
                            }
                            for (++z; z < house->m_position[2] + RaisedRoadSurfaceOffset; ++z)
                                setBlock(x, y, z, Block(plasma::Vector<3, unsigned char>(color), BlockRoad), zone);
                        }
                    }
                }

                // Pass 1: the walls of every floor cell, its base and arcs. A shop's or the inn's doors carry its
                // role.
                int doorStyle = PlacementHouse;
                switch (plot.variant) {
                case HouseVariantItemShop:
                    doorStyle = PlacementItemShop;
                    break;
                case HouseVariantArmorShop:
                    doorStyle = PlacementArmorShop;
                    break;
                case HouseVariantWeaponShop:
                    doorStyle = PlacementWeaponShop;
                    break;
                case HouseVariantAnalyzer:
                    doorStyle = PlacementAnalyzer;
                    break;
                case HouseVariantInn:
                    doorStyle = PlacementInn;
                    break;
                }
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        for (int z = 0; z < house->sizeZ(); ++z) {
                            switch (house->cell(x, y, z).kind) {
                            case HouseCellArc:
                                placeSprite(arc,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - arc->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - arc->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            house->cell(x, y, z).rotation, BlockBuilding, PlacementHouse, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                                break;
                            case HouseCellBase:
                                placeSprite(base,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - base->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - base->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            house->cell(x, y, z).rotation, BlockBuilding, PlacementHouse, zone, true,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                                break;
                            case HouseCellFloor:
                                // The side towards (x, y - 1).
                                if (house->cell(x, y - 1, z).kind == HouseCellEmpty) {
                                    if (!house->cell(x, y, z).selected &&
                                        (house->cell(x, y - 1, z - 1).kind == HouseCellEmpty ||
                                         house->cell(x, y - 1, z - 1).kind == HouseCellRoof)) {
                                        // An outer wall: sometimes a lamp on the first floor or a balcony above it, otherwise a window.
                                        if (z == 1 && rand() % WallDecorationChanceDenominator == 0)
                                            placeSprite(
                                                wallLamp,
                                                plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                           x * HouseCellWidth - wallLamp->sizeX() / 2,
                                                                       house->m_position[1] + HouseCellCenterOffset +
                                                                           y * HouseCellWidth - wallLamp->sizeY() / 2,
                                                                       house->m_position[2] + z * HouseStoreyHeight),
                                                0, BlockBuilding, PlacementHouse, zone, false,
                                                plasma::Vector<4, int>(
                                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                0);
                                        else if (z > 1 && rand() % 6 == 0)
                                            placeSprite(wallBalcony,
                                                        plasma::Vector<3, int>(
                                                            house->m_position[0] + HouseCellCenterOffset +
                                                                x * HouseCellWidth - wallBalcony->sizeX() / 2,
                                                            house->m_position[1] + HouseCellCenterOffset +
                                                                y * HouseCellWidth - wallBalcony->sizeY() / 2,
                                                            house->m_position[2] + z * HouseStoreyHeight),
                                                        0, BlockBuilding, PlacementHouse, zone, false,
                                                        plasma::Vector<4, int>(
                                                            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                        0);
                                        else
                                            placeSprite(
                                                wallWindow,
                                                plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                           x * HouseCellWidth - wallWindow->sizeX() / 2,
                                                                       house->m_position[1] + HouseCellCenterOffset +
                                                                           y * HouseCellWidth - wallWindow->sizeY() / 2,
                                                                       house->m_position[2] + z * HouseStoreyHeight),
                                                0, BlockBuilding, PlacementHouse, zone, false,
                                                plasma::Vector<4, int>(
                                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                0);
                                    } else {
                                        placeSprite(
                                            wallDoor,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - wallDoor->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - wallDoor->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            0, BlockBuilding, doorStyle, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                                        // The selected cell gets the entrance stairs outside its door.
                                        if (house->cell(x, y, z).selected)
                                            placeSprite(entranceStairs,
                                                        plasma::Vector<3, int>(
                                                            house->m_position[0] + HouseCellCenterOffset +
                                                                x * HouseCellWidth - entranceStairs->sizeX() / 2,
                                                            house->m_position[1] + HouseCellCenterOffset +
                                                                y * HouseCellWidth - HouseCellWidth -
                                                                entranceStairs->sizeY() / 2,
                                                            house->m_position[2] + z * HouseStoreyHeight -
                                                                EntranceStairVerticalOffset),
                                                        0, BlockBuilding, doorStyle, zone, true,
                                                        plasma::Vector<4, int>(
                                                            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                        0);
                                    }
                                } else if (house->cell(x, y - 1, z).kind != HouseCellFloor) {
                                    placeSprite(
                                        wall,
                                        plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                   x * HouseCellWidth - wall->sizeX() / 2,
                                                               house->m_position[1] + HouseCellCenterOffset +
                                                                   y * HouseCellWidth - wall->sizeY() / 2,
                                                               house->m_position[2] + z * HouseStoreyHeight),
                                        0, BlockBuilding, PlacementHouse, zone, false,
                                        plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                                }
                                // The side towards (x, y + 1).
                                if (house->cell(x, y + 1, z).kind == HouseCellEmpty) {
                                    if (!house->cell(x, y, z).selected &&
                                        (house->cell(x, y + 1, z - 1).kind == HouseCellEmpty ||
                                         house->cell(x, y + 1, z - 1).kind == HouseCellRoof)) {
                                        // An outer wall: sometimes a lamp on the first floor or a balcony above it, otherwise a window.
                                        if (z == 1 && rand() % 6 == 0)
                                            placeSprite(
                                                wallLamp,
                                                plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                           x * HouseCellWidth - wallLamp->sizeX() / 2,
                                                                       house->m_position[1] + HouseCellCenterOffset +
                                                                           y * HouseCellWidth - wallLamp->sizeY() / 2,
                                                                       house->m_position[2] + z * HouseStoreyHeight),
                                                2, BlockBuilding, PlacementHouse, zone, false,
                                                plasma::Vector<4, int>(
                                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                0);
                                        else if (z > 1 && rand() % 6 == 0)
                                            placeSprite(wallBalcony,
                                                        plasma::Vector<3, int>(
                                                            house->m_position[0] + HouseCellCenterOffset +
                                                                x * HouseCellWidth - wallBalcony->sizeX() / 2,
                                                            house->m_position[1] + HouseCellCenterOffset +
                                                                y * HouseCellWidth - wallBalcony->sizeY() / 2,
                                                            house->m_position[2] + z * HouseStoreyHeight),
                                                        2, BlockBuilding, PlacementHouse, zone, false,
                                                        plasma::Vector<4, int>(
                                                            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                        0);
                                        else
                                            placeSprite(
                                                wallWindow,
                                                plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                           x * HouseCellWidth - wallWindow->sizeX() / 2,
                                                                       house->m_position[1] + HouseCellCenterOffset +
                                                                           y * HouseCellWidth - wallWindow->sizeY() / 2,
                                                                       house->m_position[2] + z * HouseStoreyHeight),
                                                2, BlockBuilding, PlacementHouse, zone, false,
                                                plasma::Vector<4, int>(
                                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                0);
                                    } else {
                                        placeSprite(
                                            wallDoor,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - wallDoor->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - wallDoor->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            2, BlockBuilding, doorStyle, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                                        // The selected cell gets the entrance stairs outside its door.
                                        if (house->cell(x, y, z).selected)
                                            placeSprite(entranceStairs,
                                                        plasma::Vector<3, int>(
                                                            house->m_position[0] + HouseCellCenterOffset +
                                                                x * HouseCellWidth - entranceStairs->sizeX() / 2,
                                                            house->m_position[1] + HouseCellCenterOffset +
                                                                y * HouseCellWidth + HouseCellWidth -
                                                                entranceStairs->sizeY() / 2,
                                                            house->m_position[2] + z * HouseStoreyHeight - 4),
                                                        2, BlockBuilding, doorStyle, zone, true,
                                                        plasma::Vector<4, int>(
                                                            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                        0);
                                    }
                                } else if (house->cell(x, y + 1, z).kind != HouseCellFloor) {
                                    placeSprite(
                                        wall,
                                        plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                   x * HouseCellWidth - wall->sizeX() / 2,
                                                               house->m_position[1] + HouseCellCenterOffset +
                                                                   y * HouseCellWidth - wall->sizeY() / 2,
                                                               house->m_position[2] + z * HouseStoreyHeight),
                                        2, BlockBuilding, PlacementHouse, zone, false,
                                        plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                                }
                                // The side towards (x - 1, y).
                                if (house->cell(x - 1, y, z).kind == HouseCellEmpty) {
                                    if (!house->cell(x, y, z).selected &&
                                        (house->cell(x - 1, y, z - 1).kind == HouseCellEmpty ||
                                         house->cell(x - 1, y, z - 1).kind == HouseCellRoof)) {
                                        // An outer wall: sometimes a lamp on the first floor or a balcony above it, otherwise a window.
                                        if (z == 1 && rand() % 6 == 0)
                                            placeSprite(
                                                wallLamp,
                                                plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                           x * HouseCellWidth - wallLamp->sizeX() / 2,
                                                                       house->m_position[1] + HouseCellCenterOffset +
                                                                           y * HouseCellWidth - wallLamp->sizeY() / 2,
                                                                       house->m_position[2] + z * HouseStoreyHeight),
                                                1, BlockBuilding, PlacementHouse, zone, false,
                                                plasma::Vector<4, int>(
                                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                0);
                                        else if (z > 1 && rand() % 6 == 0)
                                            placeSprite(wallBalcony,
                                                        plasma::Vector<3, int>(
                                                            house->m_position[0] + HouseCellCenterOffset +
                                                                x * HouseCellWidth - wallBalcony->sizeX() / 2,
                                                            house->m_position[1] + HouseCellCenterOffset +
                                                                y * HouseCellWidth - wallBalcony->sizeY() / 2,
                                                            house->m_position[2] + z * HouseStoreyHeight),
                                                        1, BlockBuilding, PlacementHouse, zone, false,
                                                        plasma::Vector<4, int>(
                                                            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                        0);
                                        else
                                            placeSprite(
                                                wallWindow,
                                                plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                           x * HouseCellWidth - wallWindow->sizeX() / 2,
                                                                       house->m_position[1] + HouseCellCenterOffset +
                                                                           y * HouseCellWidth - wallWindow->sizeY() / 2,
                                                                       house->m_position[2] + z * HouseStoreyHeight),
                                                1, BlockBuilding, PlacementHouse, zone, false,
                                                plasma::Vector<4, int>(
                                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                0);
                                    } else {
                                        placeSprite(
                                            wallDoor,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - wallDoor->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - wallDoor->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            1, BlockBuilding, doorStyle, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                                        // The selected cell gets the entrance stairs outside its door.
                                        if (house->cell(x, y, z).selected)
                                            placeSprite(entranceStairs,
                                                        plasma::Vector<3, int>(
                                                            house->m_position[0] + HouseCellCenterOffset +
                                                                x * HouseCellWidth - HouseCellWidth -
                                                                entranceStairs->sizeX() / 2,
                                                            house->m_position[1] + HouseCellCenterOffset +
                                                                y * HouseCellWidth - entranceStairs->sizeY() / 2,
                                                            house->m_position[2] + z * HouseStoreyHeight - 4),
                                                        1, BlockBuilding, doorStyle, zone, true,
                                                        plasma::Vector<4, int>(
                                                            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                        0);
                                    }
                                } else if (house->cell(x - 1, y, z).kind != HouseCellFloor) {
                                    placeSprite(
                                        wall,
                                        plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                   x * HouseCellWidth - wall->sizeX() / 2,
                                                               house->m_position[1] + HouseCellCenterOffset +
                                                                   y * HouseCellWidth - wall->sizeY() / 2,
                                                               house->m_position[2] + z * HouseStoreyHeight),
                                        1, BlockBuilding, PlacementHouse, zone, false,
                                        plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                                }
                                // The side towards (x + 1, y).
                                if (house->cell(x + 1, y, z).kind == HouseCellEmpty) {
                                    if (!house->cell(x, y, z).selected &&
                                        (house->cell(x + 1, y, z - 1).kind == HouseCellEmpty ||
                                         house->cell(x + 1, y, z - 1).kind == HouseCellRoof)) {
                                        // An outer wall: sometimes a lamp on the first floor or a balcony above it, otherwise a window.
                                        if (z == 1 && rand() % 6 == 0)
                                            placeSprite(
                                                wallLamp,
                                                plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                           x * HouseCellWidth - wallLamp->sizeX() / 2,
                                                                       house->m_position[1] + HouseCellCenterOffset +
                                                                           y * HouseCellWidth - wallLamp->sizeY() / 2,
                                                                       house->m_position[2] + z * HouseStoreyHeight),
                                                3, BlockBuilding, PlacementHouse, zone, false,
                                                plasma::Vector<4, int>(
                                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                0);
                                        else if (z > 1 && rand() % 6 == 0)
                                            placeSprite(wallBalcony,
                                                        plasma::Vector<3, int>(
                                                            house->m_position[0] + HouseCellCenterOffset +
                                                                x * HouseCellWidth - wallBalcony->sizeX() / 2,
                                                            house->m_position[1] + HouseCellCenterOffset +
                                                                y * HouseCellWidth - wallBalcony->sizeY() / 2,
                                                            house->m_position[2] + z * HouseStoreyHeight),
                                                        3, BlockBuilding, PlacementHouse, zone, false,
                                                        plasma::Vector<4, int>(
                                                            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                        0);
                                        else
                                            placeSprite(
                                                wallWindow,
                                                plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                           x * HouseCellWidth - wallWindow->sizeX() / 2,
                                                                       house->m_position[1] + HouseCellCenterOffset +
                                                                           y * HouseCellWidth - wallWindow->sizeY() / 2,
                                                                       house->m_position[2] + z * HouseStoreyHeight),
                                                3, BlockBuilding, PlacementHouse, zone, false,
                                                plasma::Vector<4, int>(
                                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                0);
                                    } else {
                                        placeSprite(
                                            wallDoor,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - wallDoor->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - wallDoor->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            3, BlockBuilding, doorStyle, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                                        // The selected cell gets the entrance stairs outside its door.
                                        if (house->cell(x, y, z).selected)
                                            placeSprite(entranceStairs,
                                                        plasma::Vector<3, int>(
                                                            house->m_position[0] + HouseCellCenterOffset +
                                                                x * HouseCellWidth + HouseCellWidth -
                                                                entranceStairs->sizeX() / 2,
                                                            house->m_position[1] + HouseCellCenterOffset +
                                                                y * HouseCellWidth - entranceStairs->sizeY() / 2,
                                                            house->m_position[2] + z * HouseStoreyHeight - 4),
                                                        3, BlockBuilding, doorStyle, zone, true,
                                                        plasma::Vector<4, int>(
                                                            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                                        0);
                                    }
                                } else if (house->cell(x + 1, y, z).kind != HouseCellFloor) {
                                    placeSprite(
                                        wall,
                                        plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                   x * HouseCellWidth - wall->sizeX() / 2,
                                                               house->m_position[1] + HouseCellCenterOffset +
                                                                   y * HouseCellWidth - wall->sizeY() / 2,
                                                               house->m_position[2] + z * HouseStoreyHeight),
                                        3, BlockBuilding, PlacementHouse, zone, false,
                                        plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                                }
                                break;
                            }
                        }
                    }
                }

                // Pass 2: the roofs. A roof cell joins the roofs across its ridge with the corner pieces; it is
                // drawn as two halves facing apart.
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        for (int z = 0; z < house->sizeZ(); ++z) {
                            if (house->cell(x, y, z).kind != HouseCellRoof)
                                continue;
                            Sprite *roofA = baseRoofHalf;
                            Sprite *roofB = baseRoofHalf;
                            plasma::Vector<4, int> neighbors(0, 0, 0, 0);
                            if (house->cell(x - 1, y, z).kind)
                                neighbors[0] = 1;
                            if (house->cell(x + 1, y, z).kind)
                                neighbors[2] = 1;
                            if (house->cell(x, y - 1, z).kind)
                                neighbors[1] = 1;
                            if (house->cell(x, y + 1, z).kind)
                                neighbors[3] = 1;
                            // The ridge neighbors are compared, not assigned; both cells are read without storing a result.
                            switch (house->cell(x, y, z).rotation % 4) {
                            case 0:
                                if (house->cell(x, y - 1, z).kind == HouseCellRoof)
                                    house->cell(x, y - 1, z).rotation == house->cell(x, y, z).rotation;
                                if (house->cell(x, y + 1, z).kind == HouseCellRoof)
                                    house->cell(x, y + 1, z).rotation == house->cell(x, y, z).rotation;
                                if (house->cell(x - 1, y, z).kind == HouseCellRoof)
                                    roofA = house->cell(x - 1, y, z).rotation % 2 == house->cell(x, y, z).rotation % 2
                                                ? parallelJoinRoofHalf
                                                : perpendicularJoinRoofHalf;
                                if (house->cell(x + 1, y, z).kind == HouseCellRoof)
                                    roofB = house->cell(x + 1, y, z).rotation % 2 == house->cell(x, y, z).rotation % 2
                                                ? parallelJoinRoofHalf
                                                : perpendicularJoinRoofHalf;
                                break;
                            case 1:
                                if (house->cell(x - 1, y, z).kind == HouseCellRoof)
                                    house->cell(x - 1, y, z).rotation == house->cell(x, y, z).rotation;
                                if (house->cell(x + 1, y, z).kind == HouseCellRoof)
                                    house->cell(x + 1, y, z).rotation == house->cell(x, y, z).rotation;
                                if (house->cell(x, y + 1, z).kind == HouseCellRoof)
                                    roofA = house->cell(x, y + 1, z).rotation % 2 == house->cell(x, y, z).rotation % 2
                                                ? parallelJoinRoofHalf
                                                : perpendicularJoinRoofHalf;
                                if (house->cell(x, y - 1, z).kind == HouseCellRoof)
                                    roofB = house->cell(x, y - 1, z).rotation % 2 == house->cell(x, y, z).rotation % 2
                                                ? parallelJoinRoofHalf
                                                : perpendicularJoinRoofHalf;
                                break;
                            case 2:
                                if (house->cell(x, y + 1, z).kind == HouseCellRoof)
                                    house->cell(x, y + 1, z).rotation == house->cell(x, y, z).rotation;
                                if (house->cell(x, y - 1, z).kind == HouseCellRoof)
                                    house->cell(x, y - 1, z).rotation == house->cell(x, y, z).rotation;
                                if (house->cell(x + 1, y, z).kind == HouseCellRoof)
                                    roofA = house->cell(x + 1, y, z).rotation % 2 == house->cell(x, y, z).rotation % 2
                                                ? parallelJoinRoofHalf
                                                : perpendicularJoinRoofHalf;
                                if (house->cell(x - 1, y, z).kind == HouseCellRoof)
                                    roofB = house->cell(x - 1, y, z).rotation % 2 == house->cell(x, y, z).rotation % 2
                                                ? parallelJoinRoofHalf
                                                : perpendicularJoinRoofHalf;
                                break;
                            case 3:
                                if (house->cell(x + 1, y, z).kind == HouseCellRoof)
                                    house->cell(x + 1, y, z).rotation == house->cell(x, y, z).rotation;
                                if (house->cell(x - 1, y, z).kind == HouseCellRoof)
                                    house->cell(x - 1, y, z).rotation == house->cell(x, y, z).rotation;
                                if (house->cell(x, y - 1, z).kind == HouseCellRoof)
                                    roofA = house->cell(x, y - 1, z).rotation % 2 == house->cell(x, y, z).rotation % 2
                                                ? parallelJoinRoofHalf
                                                : perpendicularJoinRoofHalf;
                                // The kind is tested twice here.
                                if (house->cell(x, y + 1, z).kind == HouseCellRoof)
                                    roofB = house->cell(x, y + 1, z).kind == HouseCellRoof &&
                                                    house->cell(x, y + 1, z).rotation % 2 ==
                                                        house->cell(x, y, z).rotation % 2
                                                ? parallelJoinRoofHalf
                                                : perpendicularJoinRoofHalf;
                                break;
                            }
                            placeSprite(roofA,
                                        plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                   x * HouseCellWidth - roofA->sizeX() / 2,
                                                               house->m_position[1] + HouseCellCenterOffset +
                                                                   y * HouseCellWidth - roofA->sizeY() / 2,
                                                               house->m_position[2] + z * HouseStoreyHeight),
                                        house->cell(x, y, z).rotation, 14, PlacementHouse, zone, false, neighbors, 0);
                            placeSprite(roofB,
                                        plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                   x * HouseCellWidth - roofB->sizeX() / 2,
                                                               house->m_position[1] + HouseCellCenterOffset +
                                                                   y * HouseCellWidth - roofB->sizeY() / 2,
                                                               house->m_position[2] + z * HouseStoreyHeight),
                                        (house->cell(x, y, z).rotation + 2) % 4, 14, PlacementHouse, zone, false,
                                        neighbors, 0);
                        }
                    }
                }

                // Pass 3: the floors (under a roof, the ceiling of the floor cell below it), the stair floors and
                // the carpets.
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        for (int z = 0; z < house->sizeZ(); ++z) {
                            switch (house->cell(x, y, z).kind) {
                            case HouseCellFloor:
                                if (house->cell(x, y, z).stairs) {
                                    placeSprite(
                                        floorStairs,
                                        plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                   x * HouseCellWidth - floorStairs->sizeX() / 2,
                                                               house->m_position[1] + HouseCellCenterOffset +
                                                                   y * HouseCellWidth - floorStairs->sizeY() / 2,
                                                               house->m_position[2] + z * HouseStoreyHeight -
                                                                   floorStairs->sizeZ() + 1),
                                        house->cell(x, y, z).rotation, BlockBuilding, PlacementHouse, zone, false,
                                        plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                                    break;
                                }
                                // Some floors of a village house get a carpet.
                                if (record->kind == Region::Record::SceneryCity && rand() % 2 == 0 &&
                                    !house->cell(x, y, z + 1).stairs) {
                                    Sprite *carpet =
                                        m_spriteManager.sprite(SpriteCarpet1 + rand() % CarpetVariantCount);
                                    placeSprite(
                                        carpet,
                                        plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                   x * HouseCellWidth - carpet->sizeX() / 2,
                                                               house->m_position[1] + HouseCellCenterOffset +
                                                                   y * HouseCellWidth - carpet->sizeY() / 2,
                                                               house->m_position[2] + z * HouseStoreyHeight -
                                                                   carpet->sizeZ() + 1),
                                        rand() % 4, BlockBuilding, PlacementHouse, zone, false,
                                        plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                                }
                                placeSprite(floor,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - floor->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - floor->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight -
                                                                       floor->sizeZ() + 1),
                                            house->cell(x, y, z).rotation, BlockBuilding, PlacementHouse, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                                break;
                            case HouseCellRoof:
                                if (house->cell(x, y, z - 1).kind == HouseCellFloor) {
                                    placeSprite(
                                        floor,
                                        plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                   x * HouseCellWidth - floor->sizeX() / 2,
                                                               house->m_position[1] + HouseCellCenterOffset +
                                                                   y * HouseCellWidth - floor->sizeY() / 2,
                                                               house->m_position[2] + z * HouseStoreyHeight -
                                                                   floor->sizeZ() + 1),
                                        house->cell(x, y, z).rotation, BlockBuilding, PlacementHouse, zone, false,
                                        plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                                }
                                break;
                            }
                        }
                    }
                }

                // Pass 4: inner walls between each bedroom and the floor cells around it.
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        for (int z = 0; z < house->sizeZ(); ++z) {
                            if (house->cell(x, y, z).kind != HouseCellFloor ||
                                house->cell(x, y, z).variant != HouseCellBedroom)
                                continue;
                            if (house->cell(x, y - 1, z).kind == HouseCellFloor)
                                placeSprite(wallIndoor,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - wallIndoor->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - wallIndoor->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            0, BlockBuilding, PlacementHouse, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                            if (house->cell(x - 1, y, z).kind == HouseCellFloor)
                                placeSprite(wallIndoor,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - wallIndoor->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - wallIndoor->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            1, BlockBuilding, PlacementHouse, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                            if (house->cell(x, y + 1, z).kind == HouseCellFloor)
                                placeSprite(wallIndoor,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - wallIndoor->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - wallIndoor->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            2, BlockBuilding, PlacementHouse, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                            if (house->cell(x + 1, y, z).kind == HouseCellFloor)
                                placeSprite(wallIndoor,
                                            plasma::Vector<3, int>(house->m_position[0] + HouseCellCenterOffset +
                                                                       x * HouseCellWidth - wallIndoor->sizeX() / 2,
                                                                   house->m_position[1] + HouseCellCenterOffset +
                                                                       y * HouseCellWidth - wallIndoor->sizeY() / 2,
                                                                   house->m_position[2] + z * HouseStoreyHeight),
                                            3, BlockBuilding, PlacementHouse, zone, false,
                                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)),
                                            0);
                        }
                    }
                }

                // Pass 5: mark the air inside every floor cell as interior, and note the ground-floor cells: all
                // of them, the entrance and the variant-3 rooms.
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        for (int z = 0; z < house->sizeZ(); ++z) {
                            if (house->cell(x, y, z).kind != HouseCellFloor)
                                // Interior room layout: cell extents and top offset.
                                continue;
                            for (int i = 0; i < 14; ++i) {
                                for (int j = 0; j < 14; ++j) {
                                    for (int k = 5; k >= 0; --k) {
                                        plasma::Vector<3, int> block(house->m_position[0] + x * HouseCellWidth + i,
                                                                     house->m_position[1] + y * HouseCellWidth + j,
                                                                     house->m_position[2] + z * HouseStoreyHeight + k);
                                        const Block &current = getBlock(block[0], block[1], block[2], zone);
                                        if (!getBlock(block[0], block[1], block[2], zone).isInterior())
                                            setBlock(block[0], block[1], block[2],
                                                     Block(current.color, (1 << BlockFlagInterior)), zone);
                                    }
                                }
                            }
                            if (house->cell(x, y, z - 1).kind != HouseCellBase)
                                continue;
                            if (house->cell(x, y, z).selected)
                                house->entrances.push_back(plasma::Vector<3, int>(
                                    house->m_position[0] + HouseCellCenterOffset + x * HouseCellWidth,
                                    house->m_position[1] + HouseCellCenterOffset + y * HouseCellWidth,
                                    house->m_position[2] + z * HouseStoreyHeight + 1));
                            if (house->cell(x, y, z).variant == HouseCellCounter)
                                house->counters.push_back(plasma::Vector<3, int>(
                                    house->m_position[0] + HouseCellCenterOffset + x * HouseCellWidth,
                                    house->m_position[1] + HouseCellCenterOffset + y * HouseCellWidth,
                                    house->m_position[2] + z * HouseStoreyHeight + 1));
                            house->rooms.push_back(plasma::Vector<3, int>(
                                house->m_position[0] + HouseCellCenterOffset + x * HouseCellWidth,
                                house->m_position[1] + HouseCellCenterOffset + y * HouseCellWidth,
                                house->m_position[2] + z * HouseStoreyHeight + 1));
                        }
                    }
                }

                // Pass 6: a village house's lamps and furniture: objects along the outer walls of each floor cell,
                // then the room's furniture by the cell's variant (beds, a table with stools, shelves, or
                // sometimes a single table).
                for (int x = 0; x < house->sizeX(); ++x) {
                    for (int y = 0; y < house->sizeY(); ++y) {
                        for (int z = 0; z < house->sizeZ(); ++z) {
                            if (record->kind != Region::Record::SceneryCity)
                                continue;
                            if (house->cell(x, y, z).kind == HouseCellFloor && (x + y) % 2) {
                                Zone::Plant lamp;
                                lamp.type = PlantLantern02;
                                lamp.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + HouseCellCenterOffset + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + HouseCellCenterOffset + y * HouseCellWidth,
                                    Fixed(house->m_position[2]) + z * HouseStoreyHeight + 5.3);
                                lamp.scale = 0.075f;
                                lamp.rotation = 0.0f;
                                // Ceiling lantern warm RGB light palette.
                                lamp.flags = (1 << PlantFlagEmissive);
                                lamp.color = plasma::Vector<3, float>(0.6f, 0.5f, 0.4f);
                                zone->m_plants.push_back(lamp);
                            }
                            const int style = house->cell(x, y, z).variant;
                            if (house->cell(x, y, z).kind != HouseCellFloor || house->cell(x, y, z + 1).stairs)
                                continue;
                            if (house->cell(x - 1, y, z).kind != HouseCellFloor) {
                                if (rand() % InteriorFurnitureChanceDenominator) {
                                    // Furniture layout: dimensions and wall/center offsets for this room type.
                                    Zone::Object object =
                                        makeInteriorObject(plasma::Vector<3, Fixed>(
                                                               Fixed(house->m_position[0]) + 1.5 + x * HouseCellWidth,
                                                               Fixed(house->m_position[1]) + 4.5 + y * HouseCellWidth,
                                                               Fixed(house->m_position[2] + z * HouseStoreyHeight + 1)),
                                                           1, style);
                                    if (getBlock(object.position[0], object.position[1], object.position[2] - 0.5, zone)
                                            .isSolid())
                                        zone->m_objects.push_back(object);
                                }
                                if (rand() % 3) {
                                    Zone::Object object =
                                        makeInteriorObject(plasma::Vector<3, Fixed>(
                                                               Fixed(house->m_position[0]) + 1.5 + x * HouseCellWidth,
                                                               Fixed(house->m_position[1]) + 9.5 + y * HouseCellWidth,
                                                               Fixed(house->m_position[2] + z * HouseStoreyHeight + 1)),
                                                           1, style);
                                    if (getBlock(object.position[0], object.position[1], object.position[2] - 0.5, zone)
                                            .isSolid())
                                        zone->m_objects.push_back(object);
                                }
                            }
                            if (house->cell(x + 1, y, z).kind != HouseCellFloor) {
                                if (rand() % 3) {
                                    Zone::Object object =
                                        makeInteriorObject(plasma::Vector<3, Fixed>(
                                                               Fixed(house->m_position[0]) + 12.5 + x * HouseCellWidth,
                                                               Fixed(house->m_position[1]) + 4.5 + y * HouseCellWidth,
                                                               Fixed(house->m_position[2] + z * HouseStoreyHeight + 1)),
                                                           3, style);
                                    if (getBlock(object.position[0], object.position[1], object.position[2] - 0.5, zone)
                                            .isSolid())
                                        zone->m_objects.push_back(object);
                                }
                                if (rand() % 3) {
                                    Zone::Object object =
                                        makeInteriorObject(plasma::Vector<3, Fixed>(
                                                               Fixed(house->m_position[0]) + 12.5 + x * HouseCellWidth,
                                                               Fixed(house->m_position[1]) + 9.5 + y * HouseCellWidth,
                                                               Fixed(house->m_position[2] + z * HouseStoreyHeight + 1)),
                                                           3, style);
                                    if (getBlock(object.position[0], object.position[1], object.position[2] - 0.5, zone)
                                            .isSolid())
                                        zone->m_objects.push_back(object);
                                }
                            }
                            if (house->cell(x, y - 1, z).kind != HouseCellFloor) {
                                {
                                    Zone::Object object =
                                        makeInteriorObject(plasma::Vector<3, Fixed>(
                                                               Fixed(house->m_position[0]) + 4.5 + x * HouseCellWidth,
                                                               Fixed(house->m_position[1]) + 1.5 + y * HouseCellWidth,
                                                               Fixed(house->m_position[2] + z * HouseStoreyHeight + 1)),
                                                           2, style);
                                    if (getBlock(object.position[0], object.position[1], object.position[2] - 0.5, zone)
                                            .isSolid())
                                        zone->m_objects.push_back(object);
                                }
                            }
                            if (house->cell(x, y + 1, z).kind != HouseCellFloor) {
                                if (rand() % 3) {
                                    Zone::Object object =
                                        makeInteriorObject(plasma::Vector<3, Fixed>(
                                                               Fixed(house->m_position[0]) + 4.5 + x * HouseCellWidth,
                                                               Fixed(house->m_position[1]) + 12.5 + y * HouseCellWidth,
                                                               Fixed(house->m_position[2] + z * HouseStoreyHeight + 1)),
                                                           0, style);
                                    if (getBlock(object.position[0], object.position[1], object.position[2] - 0.5, zone)
                                            .isSolid())
                                        zone->m_objects.push_back(object);
                                }
                                if (rand() % 3) {
                                    Zone::Object object =
                                        makeInteriorObject(plasma::Vector<3, Fixed>(
                                                               Fixed(house->m_position[0]) + 9.5 + x * HouseCellWidth,
                                                               Fixed(house->m_position[1]) + 12.5 + y * HouseCellWidth,
                                                               Fixed(house->m_position[2] + z * HouseStoreyHeight + 1)),
                                                           0, style);
                                    if (getBlock(object.position[0], object.position[1], object.position[2] - 0.5, zone)
                                            .isSolid())
                                        zone->m_objects.push_back(object);
                                }
                            }
                            switch (house->cell(x, y, z).variant) {
                            case HouseCellBedroom: {
                                Zone::Object bed;
                                bed.type = StaticObjectBed;
                                bed.size = plasma::Vector<3, float>(2.0f, 3.0f, 1.0f);
                                bed.position =
                                    plasma::Vector<3, Fixed>(Fixed(house->m_position[0]) + 4 + x * HouseCellWidth,
                                                             Fixed(house->m_position[1]) + 4 + y * HouseCellWidth,
                                                             Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                bed.rotation = rand() % 4;
                                if (getBlock(bed.position[0], bed.position[1], bed.position[2] - 0.5, zone).isSolid())
                                    zone->m_objects.push_back(bed);
                                bed.position =
                                    plasma::Vector<3, Fixed>(Fixed(house->m_position[0]) + 10 + x * HouseCellWidth,
                                                             Fixed(house->m_position[1]) + 4 + y * HouseCellWidth,
                                                             Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                bed.rotation = rand() % 4;
                                if (getBlock(bed.position[0], bed.position[1], bed.position[2] - 0.5, zone).isSolid())
                                    zone->m_objects.push_back(bed);
                                bed.position =
                                    plasma::Vector<3, Fixed>(Fixed(house->m_position[0]) + 4 + x * HouseCellWidth,
                                                             Fixed(house->m_position[1]) + 10 + y * HouseCellWidth,
                                                             Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                bed.rotation = rand() % 4;
                                if (getBlock(bed.position[0], bed.position[1], bed.position[2] - 0.5, zone).isSolid())
                                    zone->m_objects.push_back(bed);
                                bed.position =
                                    plasma::Vector<3, Fixed>(Fixed(house->m_position[0]) + 10 + x * HouseCellWidth,
                                                             Fixed(house->m_position[1]) + 10 + y * HouseCellWidth,
                                                             Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                bed.rotation = rand() % 4;
                                if (getBlock(bed.position[0], bed.position[1], bed.position[2] - 0.5, zone).isSolid())
                                    zone->m_objects.push_back(bed);
                                break;
                            }
                            case HouseCellDining: {
                                Zone::Object table;
                                table.type = StaticObjectTable;
                                table.size = plasma::Vector<3, float>(3.0f, 3.0f, 1.0f);
                                table.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + HouseCellCenterOffset + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + HouseCellCenterOffset + y * HouseCellWidth,
                                    Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                table.rotation = rand() % 4;
                                if (getBlock(table.position[0], table.position[1], table.position[2] - 0.5, zone)
                                        .isSolid())
                                    zone->m_objects.push_back(table);
                                Zone::Object stool;
                                stool.type = StaticObjectStool;
                                stool.size = plasma::Vector<3, float>(1.0f, 1.0f, 0.5f);
                                stool.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + 4.5 + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + HouseCellCenterOffset + y * HouseCellWidth,
                                    Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                stool.rotation = 1;
                                if (getBlock(stool.position[0], stool.position[1], stool.position[2] - 0.5, zone)
                                        .isSolid())
                                    zone->m_objects.push_back(stool);
                                stool.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + 9.5 + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + HouseCellCenterOffset + y * HouseCellWidth,
                                    Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                stool.rotation = 3;
                                if (getBlock(stool.position[0], stool.position[1], stool.position[2] - 0.5, zone)
                                        .isSolid())
                                    zone->m_objects.push_back(stool);
                                stool.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + HouseCellCenterOffset + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + 4.5 + y * HouseCellWidth,
                                    Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                stool.rotation = 2;
                                if (getBlock(stool.position[0], stool.position[1], stool.position[2] - 0.5, zone)
                                        .isSolid())
                                    zone->m_objects.push_back(stool);
                                stool.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + HouseCellCenterOffset + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + 9.5 + y * HouseCellWidth,
                                    Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                stool.rotation = 0;
                                if (getBlock(stool.position[0], stool.position[1], stool.position[2] - 0.5, zone)
                                        .isSolid())
                                    zone->m_objects.push_back(stool);
                                break;
                            }
                            case HouseCellCounter: {
                                Zone::Object counter;
                                counter.type = StaticObjectCounter;
                                counter.size = plasma::Vector<3, float>(4.0f, 1.0f, 1.0f);
                                counter.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + 3.5 + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + HouseCellCenterOffset + y * HouseCellWidth,
                                    Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                counter.rotation = 3;
                                if (getBlock(counter.position[0], counter.position[1], counter.position[2] - 0.5, zone)
                                        .isSolid())
                                    zone->m_objects.push_back(counter);
                                counter.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + 10.5 + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + HouseCellCenterOffset + y * HouseCellWidth,
                                    Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                counter.rotation = 1;
                                if (getBlock(counter.position[0], counter.position[1], counter.position[2] - 0.5, zone)
                                        .isSolid())
                                    zone->m_objects.push_back(counter);
                                counter.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + HouseCellCenterOffset + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + 3.5 + y * HouseCellWidth,
                                    Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                counter.rotation = 0;
                                if (getBlock(counter.position[0], counter.position[1], counter.position[2] - 0.5, zone)
                                        .isSolid())
                                    zone->m_objects.push_back(counter);
                                counter.position = plasma::Vector<3, Fixed>(
                                    Fixed(house->m_position[0]) + HouseCellCenterOffset + x * HouseCellWidth,
                                    Fixed(house->m_position[1]) + 10.5 + y * HouseCellWidth,
                                    Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                counter.rotation = 2;
                                if (getBlock(counter.position[0], counter.position[1], counter.position[2] - 0.5, zone)
                                        .isSolid())
                                    zone->m_objects.push_back(counter);
                                break;
                            }
                            default:
                                if (rand() % PlainRoomTableChanceDenominator == 0) {
                                    Zone::Object table;
                                    table.type = StaticObjectTable;
                                    table.size = plasma::Vector<3, float>(3.0f, 3.0f, 1.0f);
                                    table.position = plasma::Vector<3, Fixed>(
                                        Fixed(house->m_position[0]) + HouseCellCenterOffset + x * HouseCellWidth,
                                        Fixed(house->m_position[1]) + HouseCellCenterOffset + y * HouseCellWidth,
                                        Fixed(house->m_position[2] + z * HouseStoreyHeight + 1));
                                    table.rotation = rand() % 4;
                                    if (getBlock(table.position[0], table.position[1], table.position[2] - 0.5, zone)
                                            .isSolid())
                                        zone->m_objects.push_back(table);
                                }
                                break;
                            }
                        }
                    }
                }

                // The ids (zone x, zone y, index) of the seats and beds among the house's new objects, for its
                // residents.
                for (int i = firstObject; i < (int)zone->m_objects.size(); ++i) {
                    Zone::Object &object = zone->m_objects[i];
                    if (object.type == StaticObjectStool || object.type == StaticObjectBench)
                        house->seats.push_back(plasma::Vector<3, int>(zone->m_position[0], zone->m_position[1], i));
                    if (object.type == StaticObjectBed)
                        house->beds.push_back(plasma::Vector<3, int>(zone->m_position[0], zone->m_position[1], i));
                }

                // Pass 7: barrels, crates and sacks outside a village house's ground-floor walls, two chances in
                // six per wall.
                if (record->kind == Region::Record::SceneryCity) {
                    for (int x = 0; x < house->sizeX(); ++x) {
                        for (int y = 0; y < house->sizeY(); ++y) {
                            if (house->cell(x, y, 0).kind != HouseCellBase)
                                continue;
                            if (house->cell(x - 1, y, 0).kind == HouseCellEmpty) {
                                if (rand() % 6 == 0) {
                                    Zone::Object object = makeHouseObject(
                                        plasma::Vector<3, Fixed>(Fixed(house->m_position[0]) - 1.5 + x * HouseCellWidth,
                                                                 Fixed(house->m_position[1]) + 4 + y * HouseCellWidth,
                                                                 Fixed(house->m_position[2])),
                                        3, 0);
                                    if (settleObject(object, zone, true))
                                        zone->m_objects.push_back(object);
                                }
                                if (rand() % 6 == 0) {
                                    Zone::Object object = makeHouseObject(
                                        plasma::Vector<3, Fixed>(Fixed(house->m_position[0]) - 1.5 + x * HouseCellWidth,
                                                                 Fixed(house->m_position[1]) + 10 + y * HouseCellWidth,
                                                                 Fixed(house->m_position[2])),
                                        3, 0);
                                    if (settleObject(object, zone, true))
                                        zone->m_objects.push_back(object);
                                }
                            }
                            if (house->cell(x + 1, y, 0).kind == HouseCellEmpty) {
                                if (rand() % 6 == 0) {
                                    Zone::Object object =
                                        makeHouseObject(plasma::Vector<3, Fixed>(
                                                            Fixed(house->m_position[0]) + 15.5 + x * HouseCellWidth,
                                                            Fixed(house->m_position[1]) + 4 + y * HouseCellWidth,
                                                            Fixed(house->m_position[2])),
                                                        1, 0);
                                    if (settleObject(object, zone, true))
                                        zone->m_objects.push_back(object);
                                }
                                if (rand() % 6 == 0) {
                                    Zone::Object object =
                                        makeHouseObject(plasma::Vector<3, Fixed>(
                                                            Fixed(house->m_position[0]) + 15.5 + x * HouseCellWidth,
                                                            Fixed(house->m_position[1]) + 10 + y * HouseCellWidth,
                                                            Fixed(house->m_position[2])),
                                                        1, 0);
                                    if (settleObject(object, zone, true))
                                        zone->m_objects.push_back(object);
                                }
                            }
                            if (house->cell(x, y - 1, 0).kind == HouseCellEmpty) {
                                if (rand() % 6 == 0) {
                                    Zone::Object object = makeHouseObject(
                                        plasma::Vector<3, Fixed>(Fixed(house->m_position[0]) + 4 + x * HouseCellWidth,
                                                                 Fixed(house->m_position[1]) - 1.5 + y * HouseCellWidth,
                                                                 Fixed(house->m_position[2])),
                                        0, 0);
                                    if (settleObject(object, zone, true))
                                        zone->m_objects.push_back(object);
                                }
                                if (rand() % 6 == 0) {
                                    Zone::Object object = makeHouseObject(
                                        plasma::Vector<3, Fixed>(Fixed(house->m_position[0]) + 10 + x * HouseCellWidth,
                                                                 Fixed(house->m_position[1]) - 1.5 + y * HouseCellWidth,
                                                                 Fixed(house->m_position[2])),
                                        0, 0);
                                    if (settleObject(object, zone, true))
                                        zone->m_objects.push_back(object);
                                }
                            }
                            if (house->cell(x, y + 1, 0).kind == HouseCellEmpty) {
                                if (rand() % 6 == 0) {
                                    Zone::Object object =
                                        makeHouseObject(plasma::Vector<3, Fixed>(
                                                            Fixed(house->m_position[0]) + 4 + x * HouseCellWidth,
                                                            Fixed(house->m_position[1]) + 15.5 + y * HouseCellWidth,
                                                            Fixed(house->m_position[2])),
                                                        2, 0);
                                    if (settleObject(object, zone, true))
                                        zone->m_objects.push_back(object);
                                }
                                if (rand() % 6 == 0) {
                                    Zone::Object object =
                                        makeHouseObject(plasma::Vector<3, Fixed>(
                                                            Fixed(house->m_position[0]) + 10 + x * HouseCellWidth,
                                                            Fixed(house->m_position[1]) + 15.5 + y * HouseCellWidth,
                                                            Fixed(house->m_position[2])),
                                                        2, 0);
                                    if (settleObject(object, zone, true))
                                        zone->m_objects.push_back(object);
                                }
                            }
                        }
                    }
                }
            }

            // Animals roam the open plots near the record's centre.
            if (plot.kind != HousePlotHouse && plot.score > 0.2f) {
                for (int i = 0; i < 2; ++i) {
                    for (int j = 0; j < 2; ++j) {
                        if (rand() % OpenPlotAnimalChanceDenominator || wildRaces.empty())
                            continue;
                        const int blockX =
                            zone->m_position[0] * BlocksPerZone + plotX + i * plotSize / 2 + plotSize / 4;
                        const int blockY = zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count +
                                           j * plotSize / 2 + plotSize / 4;
                        Field *field = getField(blockX, blockY, zone);
                        if (!field)
                            continue;
                        int z = field->m_groundZ;
                        while (getBlock(blockX, blockY, z, zone).isSolid())
                            ++z;
                        Spawn *animal = new Spawn;
                        animal->position = plasma::Vector<3, Fixed>(Fixed(blockX), Fixed(blockY), Fixed(z + 1));
                        animal->rotation = rand() * 360.0f / RAND_MAX;
                        animal->race = wildRaces[rand() % wildRaces.size()];
                        int minimumLevel;
                        int maximumLevel;
                        raceLevelRange(animal->race, minimumLevel, maximumLevel);
                        animal->level = rand() % (maximumLevel - minimumLevel + 1) + minimumLevel;
                        animal->hostilityType = HostilityPet;
                        animal->rarity = record->rarity;
                        zone->m_spawns.push_back(animal);
                    }
                }
            }

            // A watch tower.
            if (plot.kind == HousePlotTower)
                // Building and airship layout: model origin, home, approach and destination offsets.
                placeSprite(m_spriteManager.sprite(SpriteFrameTower),
                            plasma::Vector<3, int>(zone->m_position[0] * BlocksPerZone + halfPlot + plotX - 16,
                                                   zone->m_position[1] * BlocksPerZone + halfPlot + plotY - 16,
                                                   plot.maximum - 7),
                            plot.rotation, BlockBuilding, PlacementAdapter, zone, true,
                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);

            // The landing place: when a neighbouring region has places to fly to, an airship waits beside it
            // with its pilot.
            if (plot.kind == HousePlotLandingPlace) {
                placeSprite(m_spriteManager.sprite(SpriteLandingplace),
                            plasma::Vector<3, int>(zone->m_position[0] * BlocksPerZone + halfPlot + plotX - 16,
                                                   zone->m_position[1] * BlocksPerZone + halfPlot + plotY - 16,
                                                   plot.maximum - 5),
                            plot.rotation, BlockBuilding, PlacementAdapter, zone, true,
                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                std::vector<RegionRecord *> destinations;
                for (int i = -1; i <= 1; ++i) {
                    for (int j = -1; j <= 1; ++j) {
                        if (i == 0 && j == 0)
                            continue;
                        RegionRecord *destination = getRegionRecord(zone->m_position[0] / ZonesPerRegion + i,
                                                                    zone->m_position[1] / ZonesPerRegion + j);
                        if (destination && destination->height > 0)
                            destinations.push_back(destination);
                    }
                }
                if (!destinations.empty()) {
                    Zone::Airship *airship = new Zone::Airship;
                    airship->heading = (float)(plot.rotation * 90);
                    const float angle = (float)((plot.rotation + 2) * 3.141592653589793 * 0.5);
                    airship->home = plasma::Vector<3, Fixed>(plasma::Vector<3, int>(
                        (int)(cos(angle) * 19.0f + (float)(zone->m_position[0] * BlocksPerZone + halfPlot + plotX)),
                        (int)(sin(angle) * 19.0f + (float)(zone->m_position[1] * BlocksPerZone + halfPlot + plotY)),
                        plot.maximum + 66));
                    const float heading = (float)(-airship->heading * 3.141592653589793 / 180.0);
                    airship->position = airship->home + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                            sin(heading) * -100.0f, cos(heading) * -100.0f, 100.0f));
                    airship->rotation = airship->heading;
                    airship->destination =
                        airship->home + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(50.0f, 50.0f, 20.0f));
                    Spawn *pilot = new Spawn;
                    pilot->hostilityType = HostilityFriendlyNpc;
                    pilot->classType = ClassArchaeologist;
                    pilot->position = airship->position;
                    pilot->race = RaceGoblinMale;
                    pilot->level = record->level;
                    pilot->spawnRadius = 512.0f;
                    pilot->rotation = airship->rotation;
                    SequentialBehavior *behavior = new SequentialBehavior;
                    behavior->m_behaviors.push_back(new LookAtPlayerBehavior);
                    pilot->m_behavior = behavior;
                    pilot->generationKey =
                        generationKey(zone->m_position[0], zone->m_position[1], (int)zone->m_airships.size());
                    zone->m_spawns.push_back(pilot);
                    zone->m_airships.push_back(airship);
                }
            }

            // The class and craft halls of a city.
            if (plot.kind == HousePlotWarriorHall || plot.kind == HousePlotRangerHall ||
                plot.kind == HousePlotRogueHall || plot.kind == HousePlotMageHall || plot.kind == HousePlotSmithy ||
                plot.kind == HousePlotCarpentersShop || plot.kind == HousePlotWeavingMill) {
                Sprite *hall = m_spriteManager.sprite(SpriteBuildingWarrior);
                switch (plot.kind - HousePlotRangerHall) {
                case HousePlotRangerHall - HousePlotRangerHall:
                    hall = m_spriteManager.sprite(SpriteBuildingRanger);
                    break;
                case HousePlotRogueHall - HousePlotRangerHall:
                    hall = m_spriteManager.sprite(SpriteBuildingRogue);
                    break;
                case HousePlotMageHall - HousePlotRangerHall:
                    hall = m_spriteManager.sprite(SpriteBuildingMage);
                    break;
                case HousePlotSmithy - HousePlotRangerHall:
                    hall = m_spriteManager.sprite(SpriteBuildingSmithy);
                    break;
                case HousePlotCarpentersShop - HousePlotRangerHall:
                    hall = m_spriteManager.sprite(SpriteBuildingCarpentersshop);
                    break;
                case HousePlotWeavingMill - HousePlotRangerHall:
                    hall = m_spriteManager.sprite(SpriteBuildingWeavingmill);
                    break;
                }
                // Stand it on the highest column under its footprint.
                int top = 0;
                for (int i = 0; i < hall->sizeX(); ++i) {
                    for (int j = 0; j < hall->sizeY(); ++j) {
                        Field *field = getField(zone->m_position[0] * BlocksPerZone + halfPlot + plotX - 16 + i,
                                                zone->m_position[1] * BlocksPerZone + halfPlot + plotY - 16 + j, zone);
                        if (field && field->getBaseZ() + field->size() > top)
                            top = field->getBaseZ() + field->size();
                    }
                }
                int style;
                switch (plot.kind - HousePlotRangerHall) {
                case HousePlotRangerHall - HousePlotRangerHall:
                    style = PlacementRangerHall;
                    break;
                case HousePlotRogueHall - HousePlotRangerHall:
                    style = PlacementRogueHall;
                    break;
                case HousePlotMageHall - HousePlotRangerHall:
                    style = PlacementMageHall;
                    break;
                case HousePlotSmithy - HousePlotRangerHall:
                    style = PlacementSmithy;
                    break;
                case HousePlotCarpentersShop - HousePlotRangerHall:
                    style = PlacementCarpentersShop;
                    break;
                case HousePlotWeavingMill - HousePlotRangerHall:
                    style = PlacementWeavingMill;
                    break;
                default:
                    style = PlacementWarriorHall;
                    break;
                }
                placeSprite(hall,
                            plasma::Vector<3, int>(zone->m_position[0] * BlocksPerZone + halfPlot + plotX - 16,
                                                   zone->m_position[1] * BlocksPerZone + halfPlot + plotY - 16,
                                                   top - 7),
                            plot.rotation, BlockBuilding, style, zone, true,
                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
            }

            // The village well, standing on the ground.
            if (plot.kind == HousePlotWell) {
                plasma::Vector<3, int> position(zone->m_position[0] * BlocksPerZone + halfPlot + plotX -
                                                    m_spriteManager.sprite(SpriteBuildingStoneWell)->sizeX() / 2,
                                                zone->m_position[1] * BlocksPerZone + halfPlot + plotY -
                                                    m_spriteManager.sprite(SpriteBuildingStoneWell)->sizeY() / 2,
                                                plot.maximum);
                while (!getBlock(position[0], position[1], position[2], zone).isSolid())
                    --position[2];
                placeSprite(m_spriteManager.sprite(SpriteBuildingStoneWell), position, plot.rotation, BlockBuilding,
                            PlacementWater, zone, true,
                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
            }

            // The ruins' monuments: a sphinx and an obelisk with a treasure each
            if (plot.kind == HousePlotSphinx) {
                int sizeX = m_spriteManager.sprite(SpriteSphinx01)->sizeX();
                int sizeY = m_spriteManager.sprite(SpriteSphinx01)->sizeY();
                if (plot.rotation % 2)
                    std::swap(sizeX, sizeY);
                plasma::Vector<3, int> position(zone->m_position[0] * BlocksPerZone + halfPlot + plotX - sizeX / 2,
                                                zone->m_position[1] * BlocksPerZone + halfPlot + plotY - sizeY / 2,
                                                plot.maximum);
                while (!getBlock(position[0], position[1], position[2], zone).isSolid())
                    --position[2];
                placeSprite(m_spriteManager.sprite(SpriteSphinx01), position, plot.rotation, BlockBuilding,
                            PlacementPlain, zone, true,
                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                // A treasure on top of the column at the monument's corner.
                const int itemX = position[0];
                const int itemY = position[1];
                Field *field = getField(itemX, itemY, zone);
                int itemZ = field->getBaseZ() + field->size();
                while (!getBlock(itemX, itemY, itemZ, zone).isSolid())
                    --itemZ;
                Zone::ZoneItem treasure;
                treasure.position = plasma::Vector<3, Fixed>(Fixed(itemX) + 0.5, Fixed(itemY) + 0.5, Fixed(itemZ + 1));
                treasure.item = randomManaCube(record->level, record->level);
                zone->m_items.push_back(treasure);
            }
            if (plot.kind == HousePlotObelisk) {
                int sizeX = m_spriteManager.sprite(SpriteObelisk)->sizeX();
                int sizeY = m_spriteManager.sprite(SpriteObelisk)->sizeY();
                if (plot.rotation % 2)
                    std::swap(sizeX, sizeY);
                plasma::Vector<3, int> position(zone->m_position[0] * BlocksPerZone + halfPlot + plotX - sizeX / 2,
                                                zone->m_position[1] * BlocksPerZone + halfPlot + plotY - sizeY / 2,
                                                plot.maximum);
                while (!getBlock(position[0], position[1], position[2], zone).isSolid())
                    --position[2];
                placeSprite(m_spriteManager.sprite(SpriteObelisk), position, plot.rotation, BlockBuilding,
                            PlacementPlain, zone, true,
                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
                // A treasure on top of the column at the monument's corner.
                const int itemX = position[0];
                const int itemY = position[1];
                Field *field = getField(itemX, itemY, zone);
                int itemZ = field->getBaseZ() + field->size();
                while (!getBlock(itemX, itemY, itemZ, zone).isSolid())
                    --itemZ;
                Zone::ZoneItem treasure;
                treasure.position = plasma::Vector<3, Fixed>(Fixed(itemX) + 0.5, Fixed(itemY) + 0.5, Fixed(itemZ + 1));
                treasure.item = randomManaCube(record->level, record->level);
                zone->m_items.push_back(treasure);
            }
            // And the antique buildings.
            if (plot.kind == HousePlotAntiqueLeaderBuilding) {
                Sprite *monument = m_spriteManager.sprite(SpriteAntiqueBuilding1 + rand() % 2);
                int sizeX = monument->sizeX();
                int sizeY = monument->sizeY();
                if (plot.rotation % 2)
                    std::swap(sizeX, sizeY);
                plasma::Vector<3, int> position(zone->m_position[0] * BlocksPerZone + halfPlot + plotX - sizeX / 2,
                                                zone->m_position[1] * BlocksPerZone + halfPlot + plotY - sizeY / 2,
                                                plot.maximum);
                while (!getBlock(position[0], position[1], position[2], zone).isSolid())
                    --position[2];
                placeSprite(monument, position, plot.rotation, BlockBuilding, PlacementPlain, zone, true,
                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
            }
            if (plot.kind == HousePlotAntiqueBuilding) {
                Sprite *monument =
                    m_spriteManager.sprite(SpriteAntiqueBuilding1 + rand() % AntiqueBuildingVariantCount);
                int sizeX = monument->sizeX();
                int sizeY = monument->sizeY();
                if (plot.rotation % 2)
                    std::swap(sizeX, sizeY);
                plasma::Vector<3, int> position(zone->m_position[0] * BlocksPerZone + halfPlot + plotX - sizeX / 2,
                                                zone->m_position[1] * BlocksPerZone + halfPlot + plotY - sizeY / 2,
                                                plot.maximum);
                while (!getBlock(position[0], position[1], position[2], zone).isSolid())
                    --position[2];
                placeSprite(monument, position, plot.rotation, BlockBuilding, PlacementPlain, zone, true,
                            plasma::Vector<4, int>(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f)), 0);
            }
        }
    }

    // The open plots near the record's centre: in a village, a round paved square with a tree and
    // benches on type-11 ground at each quarter of the plot; elsewhere a tree.
    for (int x = 0; x < count; ++x) {
        for (int y = 0; y < count; ++y) {
            if (plots[x + y * count].kind != HousePlotEmpty && plots[x + y * count].kind != HousePlotType11Ground)
                continue;
            bool used = false;
            for (int i = 0; i < 512; i += BlocksPerZone) {
                for (int j = 0; j < 512; j += BlocksPerZone) {
                    const int sampleX =
                        zone->m_position[0] * BlocksPerZone + x * BlocksPerZone / count + i / count / 2 + plotSize / 4;
                    const int sampleY =
                        zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count + j / count / 2 + plotSize / 4;
                    if (0.72 > record->influence(Fixed(sampleX), Fixed(sampleY)))
                        continue;
                    int top = getField(sampleX, sampleY, zone)->getBaseZ();
                    while (getBlock(sampleX, sampleY, top, zone).isSolid())
                        ++top;
                    if (record->kind == Region::Record::SceneryCity) {
                        if (getBlock(sampleX, sampleY, top - 1, zone).blockType() != BlockRoad)
                            continue;
                        // Plaza footprint: eight-block outer radius and its squared-distance test.
                        if (record->kind == Region::Record::SceneryCity) {
                            // Level the square at its highest column.
                            for (int bx = sampleX - 8; bx <= sampleX + 8; ++bx) {
                                for (int by = sampleY - 8; by <= sampleY + 8; ++by) {
                                    if (plasma::Vector<2, double>(sampleX + 0.5 - bx, sampleY + 0.5 - by)
                                            .squaredLength() > 64)
                                        continue;
                                    int ground = getField(bx, by, zone)->getBaseZ();
                                    while (getBlock(bx, by, ground, zone).isSolid())
                                        ++ground;
                                    top = (std::max)(ground, top);
                                }
                            }
                            // Fill it up to that height: the rim with paving stones, the inside with the centre's surface
                            // block at the top (every block of the column is that same block).
                            for (int bx = sampleX - 8; bx <= sampleX + 8; ++bx) {
                                for (int by = sampleY - 8; by <= sampleY + 8; ++by) {
                                    const double distance =
                                        plasma::Vector<2, double>(sampleX + 0.5 - bx, sampleY + 0.5 - by)
                                            .squaredLength();
                                    if (distance > 64)
                                        continue;
                                    int ground = getField(bx, by, zone)->getBaseZ();
                                    while (getBlock(bx, by, ground, zone).isSolid())
                                        ++ground;
                                    const float humidity = blendedHumidity(bx, by);
                                    const float temperature = blendedTemperature(bx, by);
                                    for (int z = ground - 1; z <= top; ++z) {
                                        if (distance >= PlazaInnerRadiusSquared) {
                                            // Paving palette: shared brightness bias and random variation for all three RGB channels.
                                            setBlock(
                                                bx, by, z,
                                                Block(plasma::Vector<3, unsigned char>(
                                                          pavingColor(bx, by, z) * (rand() * 0.1f / RAND_MAX + 0.75f)),
                                                      BlockBuilding),
                                                zone);
                                        } else {
                                            setBlock(bx, by, z,
                                                     terrainBlock(sampleX, sampleY, top, humidity, temperature, zone),
                                                     zone);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    used = true;
                    const float temperature = terrainTemperature(sampleX, sampleY, zone);
                    // Plaza tree silhouette: random radius and height ranges, with climate selecting the tree kind.
                    // The original draws the height (rand() % 8) before the radius.
                    if (top > 1)
                        buildTree(sampleX, sampleY, top + 1, rand() % 6 + 7, rand() % 8 + HouseCellWidth,
                                  temperature > 0.8f ? 3 : 5, zone);
                    if (record->kind == Region::Record::SceneryCity && rand() % PlazaBenchChanceDenominator) {
                        Zone::Object bench;
                        // Plaza bench model dimensions.
                        bench.type = StaticObjectBench;
                        bench.size = plasma::Vector<3, float>(3.0f, 0.4f, 0.4f);
                        bench.position =
                            plasma::Vector<3, Fixed>(Fixed(sampleX) + 0.5, Fixed(sampleY) + 0.5 - 8, Fixed(top));
                        bench.rotation = 0;
                        if (settleObject(bench, zone, true))
                            zone->m_objects.push_back(bench);
                    }
                    if (record->kind == Region::Record::SceneryCity && rand() % 8) {
                        Zone::Object bench;
                        bench.type = StaticObjectBench;
                        bench.size = plasma::Vector<3, float>(3.0f, 0.4f, 0.4f);
                        bench.position =
                            plasma::Vector<3, Fixed>(Fixed(sampleX) + 0.5, Fixed(sampleY) + 0.5 + 8 + 1, Fixed(top));
                        bench.rotation = 2;
                        if (settleObject(bench, zone, true))
                            zone->m_objects.push_back(bench);
                    }
                    if (record->kind == Region::Record::SceneryCity && rand() % 8) {
                        Zone::Object bench;
                        bench.type = StaticObjectBench;
                        bench.size = plasma::Vector<3, float>(3.0f, 0.4f, 0.4f);
                        bench.position =
                            plasma::Vector<3, Fixed>(Fixed(sampleX) + 0.5 - 8, Fixed(sampleY) + 0.5, Fixed(top));
                        bench.rotation = 3;
                        if (settleObject(bench, zone, true))
                            zone->m_objects.push_back(bench);
                    }
                    if (record->kind == Region::Record::SceneryCity && rand() % 8) {
                        Zone::Object bench;
                        bench.type = StaticObjectBench;
                        bench.size = plasma::Vector<3, float>(3.0f, 0.4f, 0.4f);
                        bench.position =
                            plasma::Vector<3, Fixed>(Fixed(sampleX) + 0.5 + 8 + 1, Fixed(sampleY) + 0.5, Fixed(top));
                        bench.rotation = 1;
                        if (settleObject(bench, zone, true))
                            zone->m_objects.push_back(bench);
                    }
                }
            }
            if (used) {
                // The plot centre's ground, for the villagers' walks.
                const int centerX = zone->m_position[0] * BlocksPerZone + x * BlocksPerZone / count + halfPlot;
                const int centerY = zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count + halfPlot;
                int top = getField(centerX, centerY, zone)->getBaseZ();
                while (getBlock(centerX, centerY, top, zone).isSolid())
                    ++top;
                squares.push_back(plasma::Vector<3, int>(centerX, centerY, top));
            }
        }
    }

    // The houses' people. Inns (variant 1) and shops (variants 2..5) are destinations of the
    // villagers' daily walks.
    std::vector<House *> shops;
    std::vector<House *> inns;
    for (std::vector<House *>::iterator it = zone->m_houses.begin(); it != zone->m_houses.end(); it++) {
        if ((*it)->variant == HouseVariantInn)
            inns.push_back(*it);
        if ((*it)->variant == HouseVariantItemShop || (*it)->variant == HouseVariantArmorShop ||
            (*it)->variant == HouseVariantWeaponShop || (*it)->variant == HouseVariantAnalyzer)
            shops.push_back(*it);
    }
    if (record->kind == Region::Record::SceneryCity) {
        // The zone positions of the region's other settlements and sights, where some villagers want to
        // travel.
        std::vector<plasma::Vector<2, int>> travelTargets;
        Region *region = getRegion(zone->m_position[0] / ZonesPerRegion, zone->m_position[1] / ZonesPerRegion);
        if (region) {
            Region::Record *other = region->m_records;
            for (int i = 0; i < 8; ++i) {
                for (int j = 0; j < 8; ++j, ++other) {
                    if (other->kind == Region::Record::SceneryCity || other->kind == Region::Record::SceneryPortal)
                        continue;
                    if (other->kind == Region::Record::SceneryNone) {
                        Region::Cell *cell =
                            getCell((int)other->center[0] / BlocksPerZone, (int)other->center[1] / BlocksPerZone);
                        if (!cell || !cell->kind || !cell->seed)
                            continue;
                    }
                    travelTargets.push_back(plasma::Vector<2, int>(other->center) / BlocksPerZone);
                }
            }
        }
        int minimumLevel = 1;
        int maximumLevel = FallbackMaximumVendorLevel;
        int nextTarget = 0;
        RegionRecord *regionRecord =
            getRegionRecord(zone->m_position[0] / ZonesPerRegion, zone->m_position[1] / ZonesPerRegion);
        if (regionRecord)
            regionRecord->generationRange(minimumLevel, maximumLevel);
        for (std::vector<House *>::iterator it = zone->m_houses.begin(); it != zone->m_houses.end(); it++) {
            House *house = *it;
            if (house->rooms.empty())
                continue;
            // The innkeeper and the vendors stand behind their counters.
            if (house->variant == HouseVariantInn) {
                if (house->counters.empty())
                    continue;
                Spawn *keeper = new Spawn;
                keeper->position = plasma::Vector<3, Fixed>(house->counters[rand() % house->counters.size()]);
                keeper->hostilityType = HostilityFriendlyNpc;
                keeper->race = rand() % 2 + RaceHumanMale;
                keeper->level = record->level;
                keeper->classType = ClassInnkeeper;
                SequentialBehavior *behavior = new SequentialBehavior;
                behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
                behavior->m_behaviors.push_back(new LookAtPlayerBehavior);
                WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
                walk->m_path.push_back(keeper->position);
                behavior->m_behaviors.push_back(walk);
                keeper->m_behavior = behavior;
                zone->m_spawns.push_back(keeper);
            } else if (house->variant == HouseVariantItemShop) {
                if (house->counters.empty())
                    continue;
                Spawn *keeper = new Spawn;
                keeper->position = plasma::Vector<3, Fixed>(house->counters[rand() % house->counters.size()]);
                keeper->hostilityType = HostilityFriendlyNpc;
                keeper->race = rand() % 2 + RaceHumanMale;
                keeper->level = record->level;
                keeper->classType = ClassItemVendor;
                fillGeneralShop(keeper, minimumLevel, maximumLevel);
                SequentialBehavior *behavior = new SequentialBehavior;
                behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
                behavior->m_behaviors.push_back(new LookAtPlayerBehavior);
                WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
                walk->m_path.push_back(keeper->position);
                behavior->m_behaviors.push_back(walk);
                keeper->m_behavior = behavior;
                zone->m_spawns.push_back(keeper);
            } else if (house->variant == HouseVariantArmorShop) {
                if (house->counters.empty())
                    continue;
                Spawn *keeper = new Spawn;
                keeper->position = plasma::Vector<3, Fixed>(house->counters[rand() % house->counters.size()]);
                keeper->hostilityType = HostilityFriendlyNpc;
                keeper->race = rand() % 2 + RaceHumanMale;
                keeper->level = record->level;
                keeper->classType = ClassArmorVendor;
                fillArmorShop(keeper, minimumLevel, maximumLevel);
                SequentialBehavior *behavior = new SequentialBehavior;
                behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
                behavior->m_behaviors.push_back(new LookAtPlayerBehavior);
                WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
                walk->m_path.push_back(keeper->position);
                behavior->m_behaviors.push_back(walk);
                keeper->m_behavior = behavior;
                zone->m_spawns.push_back(keeper);
            } else if (house->variant == HouseVariantWeaponShop) {
                if (house->counters.empty())
                    continue;
                Spawn *keeper = new Spawn;
                keeper->position = plasma::Vector<3, Fixed>(house->counters[rand() % house->counters.size()]);
                keeper->hostilityType = HostilityFriendlyNpc;
                keeper->race = rand() % 2 + RaceHumanMale;
                keeper->level = record->level;
                keeper->classType = ClassWeaponVendor;
                fillWeaponShop(keeper, minimumLevel, maximumLevel);
                SequentialBehavior *behavior = new SequentialBehavior;
                behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
                behavior->m_behaviors.push_back(new LookAtPlayerBehavior);
                WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
                walk->m_path.push_back(keeper->position);
                behavior->m_behaviors.push_back(walk);
                keeper->m_behavior = behavior;
                zone->m_spawns.push_back(keeper);
            } else if (house->variant == HouseVariantAnalyzer) {
                if (house->counters.empty())
                    continue;
                Spawn *keeper = new Spawn;
                keeper->position = plasma::Vector<3, Fixed>(house->counters[rand() % house->counters.size()]);
                keeper->hostilityType = HostilityFriendlyNpc;
                keeper->race = rand() % 2 + RaceHumanMale;
                keeper->level = record->level;
                keeper->classType = ClassAnalyzer;
                fillWeaponShop(keeper, minimumLevel, maximumLevel);
                SequentialBehavior *behavior = new SequentialBehavior;
                behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
                behavior->m_behaviors.push_back(new LookAtPlayerBehavior);
                WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
                walk->m_path.push_back(keeper->position);
                behavior->m_behaviors.push_back(walk);
                keeper->m_behavior = behavior;
                zone->m_spawns.push_back(keeper);
            } else {
                // One or two villagers live in an ordinary house. Their day: home, then (where they exist) a
                // square, the market, a shop, a square, the inn, two shops, a square, home at the entrance, and
                // home again half an hour later.
                const int villagers = rand() % 2 + 1;
                const bool noTravelTargets = travelTargets.empty();
                const bool noMarkets = plazas.empty();
                const bool noInns = inns.empty();
                const bool noShops = shops.empty();
                const bool noSquares = squares.empty();
                for (int v = 0; v < villagers; ++v) {
                    Spawn *villager = new Spawn;
                    villager->position = plasma::Vector<3, Fixed>(house->rooms[rand() % house->rooms.size()]);
                    villager->hostilityType = HostilityFriendlyNpc;
                    villager->classType = ClassArchaeologist;
                    villager->race = rand() % 2 + RaceHumanMale;
                    villager->level = record->level;
                    if (!noTravelTargets && rand() % 2 == 0) {
                        plasma::Vector<2, int> target = travelTargets[nextTarget++ % travelTargets.size()];
                        villager->questZone = plasma::Vector<3, int>(target[0], target[1], 0);
                        villager->speechExpression = Speech::Explored;
                    }
                    if (rand() % PetFoodSpeechChanceDenominator == 0)
                        villager->speechExpression = Speech::PetFood;
                    Creature::ScheduleEntry entry;
                    entry.position = villager->position;
                    entry.time = 0;
                    villager->schedule.push_back(entry);
                    int time = (rand() % VillagerDepartureTimeRangeMinutes + VillagerEarliestDepartureMinutes) *
                               (World::MillisecondsPerHour / 60);
                    if (!noSquares) {
                        entry.position = plasma::Vector<3, Fixed>(squares[rand() % squares.size()]);
                        entry.time = time;
                        villager->schedule.push_back(entry);
                        time = villager->schedule.back().time +
                               (rand() % SquareOrHomeVisitRangeMinutes + MinimumVisitMinutes) *
                                   (World::MillisecondsPerHour / 60);
                    }
                    if (!noMarkets) {
                        entry.position = plasma::Vector<3, Fixed>(plazas[rand() % plazas.size()]);
                        entry.time = time;
                        villager->schedule.push_back(entry);
                        time = villager->schedule.back().time +
                               (rand() % MarketOrInnVisitRangeMinutes + MinimumVisitMinutes) *
                                   (World::MillisecondsPerHour / 60);
                    }
                    if (!noShops) {
                        House *shop = shops[rand() % shops.size()];
                        if (!shop->entrances.empty()) {
                            entry.position = plasma::Vector<3, Fixed>(shop->entrances[rand() % shop->entrances.size()]);
                            entry.time = time;
                            villager->schedule.push_back(entry);
                            time = villager->schedule.back().time +
                                   rand() % FirstShopVisitDurationRange * (World::MillisecondsPerHour / 60);
                        }
                    }
                    if (!noSquares) {
                        entry.position = plasma::Vector<3, Fixed>(squares[rand() % squares.size()]);
                        entry.time = time;
                        villager->schedule.push_back(entry);
                        time = villager->schedule.back().time +
                               (rand() % SquareOrHomeVisitRangeMinutes + MinimumVisitMinutes) *
                                   (World::MillisecondsPerHour / 60);
                    }
                    if (!noInns) {
                        House *inn = inns[rand() % inns.size()];
                        if (!inn->entrances.empty()) {
                            entry.position = plasma::Vector<3, Fixed>(inn->entrances[rand() % inn->entrances.size()]);
                            entry.time = time;
                            villager->schedule.push_back(entry);
                            time = villager->schedule.back().time +
                                   (rand() % MarketOrInnVisitRangeMinutes + MinimumVisitMinutes) *
                                       (World::MillisecondsPerHour / 60);
                        }
                    }
                    if (!noShops) {
                        House *shop = shops[rand() % shops.size()];
                        if (!shop->entrances.empty()) {
                            entry.position = plasma::Vector<3, Fixed>(shop->entrances[rand() % shop->entrances.size()]);
                            entry.time = time;
                            villager->schedule.push_back(entry);
                            time = villager->schedule.back().time +
                                   rand() % LaterShopVisitDurationRange * (World::MillisecondsPerHour / 60);
                        }
                        shop = shops[rand() % shops.size()];
                        if (!shop->entrances.empty()) {
                            entry.position = plasma::Vector<3, Fixed>(shop->entrances[rand() % shop->entrances.size()]);
                            entry.time = time;
                            villager->schedule.push_back(entry);
                            time = villager->schedule.back().time +
                                   rand() % LaterShopVisitDurationRange * (World::MillisecondsPerHour / 60);
                        }
                    }
                    if (!noSquares) {
                        entry.position = plasma::Vector<3, Fixed>(squares[rand() % squares.size()]);
                        entry.time = time;
                        villager->schedule.push_back(entry);
                        time = villager->schedule.back().time +
                               (rand() % SquareOrHomeVisitRangeMinutes + MinimumVisitMinutes) *
                                   (World::MillisecondsPerHour / 60);
                    }
                    if (!house->entrances.empty()) {
                        // Drawn but not used.
                        rand();
                        entry.position = villager->position;
                        entry.time = time;
                        villager->schedule.push_back(entry);
                        time = villager->schedule.back().time +
                               (rand() % SquareOrHomeVisitRangeMinutes + MinimumVisitMinutes) *
                                   (World::MillisecondsPerHour / 60);
                    }
                    entry.position = villager->position;
                    entry.time = time + 1800000;
                    villager->schedule.push_back(entry);
                    SequentialBehavior *behavior = new SequentialBehavior;
                    behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
                    behavior->m_behaviors.push_back(new LookAtPlayerBehavior);
                    behavior->m_behaviors.push_back(new SpawnLocationBehavior);
                    behavior->m_behaviors.push_back(new RandomInteractionBehavior);
                    behavior->m_behaviors.push_back(new RandomWalkBehavior);
                    villager->m_behavior = behavior;
                    zone->m_spawns.push_back(villager);
                }
            }
        }
    } else {
        // The ruins' and camps' inhabitants: the races that guard them, and the groups that live in the
        // houses (the second vector of each group is not used here).
        std::vector<int> patrolRaces;
        std::vector<std::pair<std::vector<int>, std::vector<int>>> groups;
        if (record->kind != Region::Record::SceneryRuins) {
            patrolRaces.push_back(RaceOrcMale);
            patrolRaces.push_back(RaceOrcFemale);
            std::pair<std::vector<int>, std::vector<int>> group;
            group.first.push_back(RaceOgre);
            group.second.push_back(RaceCollie);
            groups.push_back(group);
        } else {
            switch (record->style) {
            case RuinAntique: {
                patrolRaces.push_back(RaceFrightener);
                patrolRaces.push_back(RaceSkeleton);
                patrolRaces.push_back(RaceZombie);
                std::pair<std::vector<int>, std::vector<int>> group;
                group.first.push_back(RaceFrightener);
                group.first.push_back(RaceSkeleton);
                // The zombies patrol twice as often.
                patrolRaces.push_back(RaceZombie);
                group.second.push_back(RaceBat);
                group.second.push_back(RaceGreenSlime);
                group.second.push_back(RacePinkSlime);
                group.second.push_back(RaceBlueSlime);
                group.second.push_back(RaceYellowSlime);
                groups.push_back(group);
                break;
            }
            case 1: {
                patrolRaces.push_back(RaceDwarfMale);
                patrolRaces.push_back(RaceDwarfFemale);
                std::pair<std::vector<int>, std::vector<int>> group;
                group.first.push_back(RaceDwarfMale);
                group.first.push_back(RaceDwarfFemale);
                group.second.push_back(RaceAlpaca);
                group.second.push_back(RaceCrow);
                groups.push_back(group);
                break;
            }
            case RuinDesert: {
                patrolRaces.push_back(RaceNomadMale);
                patrolRaces.push_back(RaceNomadFemale);
                std::pair<std::vector<int>, std::vector<int>> group;
                group.first.push_back(RaceNomadMale);
                group.first.push_back(RaceNomadFemale);
                group.second.push_back(RaceHornet);
                group.second.push_back(RaceFly);
                groups.push_back(group);
                // A djinn lives alone.
                group.first.clear();
                group.second.clear();
                group.first.push_back(RaceDjinn);
                groups.push_back(group);
                break;
            }
            case RuinJungle: {
                patrolRaces.push_back(RaceSpikeCreature);
                std::pair<std::vector<int>, std::vector<int>> group;
                group.first.push_back(RaceSpikeCreature);
                group.second.push_back(RaceParrot);
                group.second.push_back(RaceFly);
                groups.push_back(group);
                group.first.clear();
                group.second.clear();
                group.first.push_back(RaceDjinn);
                groups.push_back(group);
                break;
            }
            case 4: {
                patrolRaces.push_back(RaceUndeadMale);
                patrolRaces.push_back(RaceUndeadFemale);
                {
                    std::pair<std::vector<int>, std::vector<int>> group;
                    group.first.push_back(RaceUndeadMale);
                    group.first.push_back(RaceUndeadFemale);
                    group.second.push_back(RaceZombie);
                    groups.push_back(group);
                }
                // One kind of monster lives alone, by the record's seed.
                std::pair<std::vector<int>, std::vector<int>> group;
                switch (record->seed % 3) {
                case 1:
                    group.first.push_back(RaceWerewolf);
                    break;
                case 2:
                    group.first.push_back(RaceSkeleton);
                    break;
                default:
                    group.first.push_back(RaceVampire);
                    break;
                }
                groups.push_back(group);
                break;
            }
            default: {
                patrolRaces.push_back(RaceOrcMale);
                patrolRaces.push_back(RaceOrcFemale);
                std::pair<std::vector<int>, std::vector<int>> group;
                group.first.push_back(RaceOgre);
                group.second.push_back(RaceCollie);
                groups.push_back(group);
                break;
            }
            }
        }

        // The leader at the centre of an antique city (plot kind 20).
        for (int x = 0; x < count; ++x) {
            for (int y = 0; y < count; ++y) {
                HousePlot &plot = plots[x + y * count];
                if (plot.kind == HousePlotAntiqueLeaderBuilding) {
                    if (patrolRaces.empty())
                        continue;
                    const int blockX = zone->m_position[0] * BlocksPerZone + halfPlot + x * BlocksPerZone / count;
                    const int blockY = zone->m_position[1] * BlocksPerZone + halfPlot + y * BlocksPerZone / count;
                    Field *field = getField(blockX, blockY, zone);
                    if (!field)
                        continue;
                    int z = field->m_groundZ;
                    while (getBlock(blockX, blockY, z, zone).isSolid())
                        ++z;
                    Spawn *leader = new Spawn;
                    leader->position = plasma::Vector<3, Fixed>(Fixed(blockX), Fixed(blockY), Fixed(z + 1));
                    leader->rotation = rand() * 360.0f / RAND_MAX;
                    leader->level = record->level;
                    leader->hostilityType = HostilityEnemy;
                    leader->race = patrolRaces[rand() % patrolRaces.size()];
                    leader->rarity = record->rarity;
                    leader->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                    leader->missionResident = true;
                    leader->appearance.appearanceFlags |= (1 << AppearanceFlagTinted);
                    SequentialBehavior *behavior = new SequentialBehavior;
                    behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
                    WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
                    walk->m_path.push_back(leader->position);
                    behavior->m_behaviors.push_back(walk);
                    leader->m_behavior = behavior;
                    zone->m_spawns.push_back(leader);
                } else if (plot.kind != HousePlotHouse && plot.score > 0.2f) {
                    // Guards roam the open plots near the centre.
                    for (int i = 0; i < 2; ++i) {
                        for (int j = 0; j < 2; ++j) {
                            if (rand() % OpenPlotGuardChanceDenominator)
                                continue;
                            const int blockX = zone->m_position[0] * BlocksPerZone + x * BlocksPerZone / count +
                                               i * plotSize / 2 + plotSize / 4;
                            const int blockY = zone->m_position[1] * BlocksPerZone + y * BlocksPerZone / count +
                                               j * plotSize / 2 + plotSize / 4;
                            if (patrolRaces.empty())
                                continue;
                            Field *field = getField(blockX, blockY, zone);
                            if (!field)
                                continue;
                            int z = field->m_groundZ;
                            while (getBlock(blockX, blockY, z, zone).isSolid())
                                ++z;
                            Spawn *guard = new Spawn;
                            guard->position = plasma::Vector<3, Fixed>(Fixed(blockX), Fixed(blockY), Fixed(z + 1));
                            guard->rotation = rand() * 360.0f / RAND_MAX;
                            guard->level = record->level;
                            guard->hostilityType = HostilityEnemy;
                            guard->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                            guard->race = patrolRaces[rand() % patrolRaces.size()];
                            guard->rarity = record->rarity;
                            zone->m_spawns.push_back(guard);
                        }
                    }
                }
            }
        }

        // The houses: rings of two to four guards around some rooms, and up to one resident of a group who
        // walks between the room and a random house of the zone. Each carries up to one potion.
        for (std::vector<House *>::iterator it = zone->m_houses.begin(); it != zone->m_houses.end(); it++) {
            if ((*it)->rooms.empty())
                continue;
            std::vector<plasma::Vector<3, int>> rooms((*it)->rooms);
            if (!patrolRaces.empty()) {
                const int rings = rand() % GuardRingCountRange;
                for (int r = 0; r < rings && !rooms.empty(); ++r) {
                    const int k = rand() % rooms.size();
                    plasma::Vector<3, Fixed> center(rooms[k]);
                    rooms.erase(rooms.begin() + k);
                    const int guards = rand() % 3 + 2;
                    for (int g = 0; g < guards; ++g) {
                        const float angle = (float)(g * 2 * 3.141592653589793 / guards);
                        Spawn *guard = new Spawn;
                        guard->position = plasma::Vector<3, Fixed>(center[0] + cosf(angle) * 2.0f,
                                                                   center[1] + sinf(angle) * 2.0f, center[2] + 1);
                        guard->rotation = (float)(angle / 3.141592653589793 * 180.0 + 90.0);
                        guard->level = record->level;
                        guard->hostilityType = HostilityEnemy;
                        guard->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                        guard->race = patrolRaces[rand() % patrolRaces.size()];
                        guard->rarity = record->rarity;
                        SequentialBehavior *behavior = new SequentialBehavior;
                        behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
                        WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
                        walk->m_path.push_back(guard->position);
                        behavior->m_behaviors.push_back(walk);
                        guard->m_behavior = behavior;
                        const int potions = rand() % 2;
                        Item potion;
                        potion.category = ItemCategoryConsumable;
                        potion.id = ConsumableLifePotion;
                        potion.level = guard->level;
                        for (int n = 0; n < potions; ++n)
                            guard->inventory.add(potion);
                        zone->m_spawns.push_back(guard);
                    }
                }
            }
            if (!groups.empty()) {
                const int residents = rand() % 2;
                for (int r = 0; r < residents && !rooms.empty(); ++r) {
                    const int k = rand() % rooms.size();
                    plasma::Vector<3, Fixed> room(rooms[k]);
                    rooms.erase(rooms.begin() + k);
                    const std::pair<std::vector<int>, std::vector<int>> &group = groups[rand() % groups.size()];
                    if (group.first.empty())
                        continue;
                    Spawn *resident = new Spawn;
                    resident->position = plasma::Vector<3, Fixed>(room[0], room[1], room[2] + 1);
                    resident->rotation = rand() * 360.0f / RAND_MAX;
                    resident->level = record->level;
                    resident->hostilityType = HostilityEnemy;
                    resident->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                    resident->race = group.first[rand() % group.first.size()];
                    resident->rarity = record->rarity;
                    SequentialBehavior *behavior = new SequentialBehavior;
                    behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
                    WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
                    walk->m_path.push_back(resident->position);
                    if (!zone->m_houses.empty()) {
                        House *other = zone->m_houses[rand() % zone->m_houses.size()];
                        if (!other->rooms.empty())
                            walk->m_path.push_back(
                                plasma::Vector<3, Fixed>(other->rooms[rand() % other->rooms.size()]));
                    }
                    behavior->m_behaviors.push_back(walk);
                    resident->m_behavior = behavior;
                    const int potions = rand() % 2;
                    Item potion;
                    potion.category = ItemCategoryConsumable;
                    potion.id = ConsumableLifePotion;
                    potion.level = resident->level;
                    for (int n = 0; n < potions; ++n)
                        resident->inventory.add(potion);
                    zone->m_spawns.push_back(resident);
                }
            }
        }
    }
    return true;
}

Zone::Object makeHouseObject(const plasma::Vector<3, Fixed> &position, int rotation, int style)
{
    Zone::Object object;
    object.position = position;
    object.rotation = rotation;
    switch (rand() % 7) {
    case 0:
        // Furniture model layout: dimensions in blocks, with crate dimensions multiplied by the shared
        // random scale.
        object.type = StaticObjectBarrel;
        object.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        break;
    case 1: {
        object.type = StaticObjectCrate;
        float scale = 1.0f + rand() * 0.5f / RAND_MAX;
        object.size = plasma::Vector<3, float>(scale * 1.5f, scale * 1.5f, scale * 1.5f);
        break;
    }
    case 2: {
        object.type = StaticObjectOpenCrate;
        float scale = 1.0f + rand() * 0.5f / RAND_MAX;
        object.size = plasma::Vector<3, float>(scale * 1.5f, scale * 1.5f, scale * 0.75f);
        break;
    }
    case 3:
        object.type = StaticObjectSack;
        object.size = plasma::Vector<3, float>(1.5f, 1.5f, 1.4f);
        break;
    case 4:
        object.type = StaticObjectBench;
        object.size = plasma::Vector<3, float>(3.0f, 1.0f, 0.4f);
        break;
    case 5:
        object.type = StaticObjectStool;
        object.size = plasma::Vector<3, float>(1.0f, 1.0f, 0.5f);
        break;
    case 6:
        object.type = StaticObjectShelter;
        object.size = plasma::Vector<3, float>(3.0f, 3.0f, 2.5f);
        break;
    }
    return object;
}

namespace {
enum {
    InteriorChestChanceDenominator = 50
};
enum {
    ShelfVariantCount = 3
};
}

Zone::Object makeInteriorObject(const plasma::Vector<3, Fixed> &position, int rotation, int style)
{
    Zone::Object object;
    object.position = position;
    object.rotation = rotation;
    if (rand() % InteriorChestChanceDenominator == 0) {
        // Furniture model layout: dimensions in blocks, with crate dimensions multiplied by the shared
        // random scale.
        object.type = StaticObjectChest;
        object.size = plasma::Vector<3, float>(1.2f, 0.8f, 0.8f);
        switch (rotation) {
        case 0:
            object.position[1] += -0.5;
            break;
        case 1:
            object.position[0] += 0.5;
            break;
        case 2:
            object.position[1] += 0.5;
            break;
        case 3:
            object.position[0] += -0.5;
            break;
        }
    } else if (style == HouseCellBedroom) {
        switch (rand() % 2) {
        case 0:
            object.type = StaticObjectShelf1 + rand() % ShelfVariantCount;
            object.size = plasma::Vector<3, float>(2.0f, 1.0f, 1.57f);
            break;
        case 1:
            object.type = StaticObjectBedTable;
            object.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.0f);
            break;
        }
    } else {
        switch (rand() % 6) {
        case 0:
            object.type = StaticObjectShelf1 + rand() % ShelfVariantCount;
            object.size = plasma::Vector<3, float>(2.0f, 1.0f, 1.57f);
            break;
        case 1:
            object.type = StaticObjectBench;
            object.size = plasma::Vector<3, float>(3.0f, 1.0f, 0.4f);
            break;
        case 2:
            object.type = StaticObjectStool;
            object.size = plasma::Vector<3, float>(1.0f, 1.0f, 0.5f);
            switch (rotation) {
            case 0:
                object.position[1] -= (float)rand() / RAND_MAX;
                break;
            case 1:
                object.position[0] += (float)rand() / RAND_MAX;
                break;
            case 2:
                object.position[1] += (float)rand() / RAND_MAX;
                break;
            case 3:
                object.position[0] -= (float)rand() / RAND_MAX;
                break;
            }
            break;
        case 3:
            object.type = StaticObjectDesktop;
            object.size = plasma::Vector<3, float>(3.0f, 1.0f, 1.0f);
            switch (rotation) {
            case 0:
                object.position[1] += -0.2;
                break;
            case 1:
                object.position[0] += 0.2;
                break;
            case 2:
                object.position[1] += 0.2;
                break;
            case 3:
                object.position[0] += -0.2;
                break;
            }
            break;
        case 4:
            object.type = StaticObjectCupboard;
            object.size = plasma::Vector<3, float>(2.5f, 1.0f, 3.0f);
            switch (rotation) {
            case 0:
                object.position[1] += -0.3;
                break;
            case 1:
                object.position[0] += 0.3;
                break;
            case 2:
                object.position[1] += 0.3;
                break;
            case 3:
                object.position[0] += -0.3;
                break;
            }
            break;
        case 5:
            object.type = StaticObjectVase1 + rand() % StaticObjectVaseCount;
            object.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.0f);
            break;
        }
    }
    return Zone::Object(object);
}

Zone::Object makeSmallHouseObject(const plasma::Vector<3, Fixed> &position, int rotation, int style)
{
    Zone::Object object;
    object.position = position;
    object.rotation = rotation;
    switch (rand() % 4) {
    case 0:
        // Furniture model layout: dimensions in blocks, with crate dimensions multiplied by the shared
        // random scale.
        object.type = StaticObjectBarrel;
        object.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        break;
    case 1: {
        object.type = StaticObjectCrate;
        float scale = 1.0f + rand() * 0.5f / RAND_MAX;
        object.size = plasma::Vector<3, float>(scale * 1.5f, scale * 1.5f, scale * 1.5f);
        break;
    }
    case 2: {
        object.type = StaticObjectOpenCrate;
        float scale = 1.0f + rand() * 0.5f / RAND_MAX;
        object.size = plasma::Vector<3, float>(scale * 1.5f, scale * 1.5f, scale * 0.75f);
        break;
    }
    case 3:
        object.type = StaticObjectSack;
        object.size = plasma::Vector<3, float>(1.5f, 1.5f, 1.4f);
        break;
    }
    return object;
}

namespace {
enum {
    MarketStandVariantCount = 3
};
}

Zone::Object makeLargeHouseObject(const plasma::Vector<3, Fixed> &position, int rotation, int style)
{
    Zone::Object object;
    object.position = position;
    object.rotation = rotation;
    // Furniture model layout: dimensions in blocks, with crate dimensions multiplied by the shared
    // random scale.
    object.type = StaticObjectMarketStand1 + rand() % MarketStandVariantCount;
    object.size = plasma::Vector<3, float>(3.5f, 2.0f, 3.0f);
    return object;
}

// A zone's generated entries use eight low bits for the entry index.
long long generationKey(int x, int y, int index)
{
    return ((long long)y * GeneratedCreatureZoneRadix + x) * GeneratedCreatureIndexRadix + index;
}

}
