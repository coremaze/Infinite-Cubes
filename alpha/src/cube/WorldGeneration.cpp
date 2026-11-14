#include "cube/WorldDimensions.h"
// World generation builds regions, terrain, dungeons, buildings, plants and creature spawns.
#include "cube/Dungeon.h"
#include "cube/Region.h"
#include "cube/WorldBlocks.h"
#include "cube/Item.h"
#include "cube/WorldGeneration.h"
#include "cube/RaceIds.h"
#include "cube/CreatureIds.h"
#include "cube/Spawn.h"
#include "cube/StatCurves.h"
#include "cube/Noise.h"
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <sstream>
#include "cube/Serializer.h"
#include "cube/Sprite.h"
#include "cube/House.h"
#include "cube/WalkPathBehavior.h"
#include "cube/SequentialBehavior.h"
#include "cube/CompanionBehavior.h"
#include "cube/CombatBehavior.h"
#include "cube/RandomInteractionBehavior.h"
#include "cube/RandomWalkBehavior.h"
#include "cube/LookAtPlayerBehavior.h"
#include "cube/ServerUpdates.h"
#include <string.h>

#include "cube/SpriteIds.h"

namespace cube {

Dungeon::Dungeon(int sizeX, int sizeY, int sizeZ)
    : m_rotation(0),
      m_reflectY(false),
      m_sizeX(sizeX),
      m_sizeY(sizeY),
      m_sizeZ(sizeZ)
{
    m_cells = new Cell[sizeX * sizeY * sizeZ];
}

Zone::GenerationMarker::GenerationMarker()
    : kind(GenerationMarkerNone),
      spawnIndex(-1),
      race(-1)
{
    position[0] = 0;
    position[1] = 0;
    position[2] = 0;
}

Region::Region()
    : m_needsServerUpdate(true),
      m_level(1),
      m_rarity(ItemRarityWhite),
      m_style(0),
      m_populated(false)
{
    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++)
            m_zones[x][y] = 0;
    }
}

Dungeon::~Dungeon()
{
    delete[] m_cells;
}

Region::~Region()
{
    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++)
            delete m_zones[x][y];
    }
}

plasma::Vector<3, float> World::beachColor(int x, int y)
// Beach brightness/blend noise frequencies.
{
    float light = (noise(m_sandColorNoiseOffsets[0] + x * 0.03, m_sandColorNoiseOffsets[1] + y * 0.03) + 1.0f) * 0.5f;
    // Procedural RGB palette and brightness coefficients for this surface.
    float blend = (noise(m_sandColorNoiseOffsets[2] + x * 0.01, m_sandColorNoiseOffsets[3] + y * 0.01) + 1.0f) * 0.5f;
    return clampColor(plasma::Vector<3, float>(240.0f, 240.0f, 100.0f) * (1.0f - blend) +
                      plasma::Vector<3, float>(240.0f, 180.0f, 50.0f) * blend +
                      plasma::Vector<3, float>(light, light, light) * 80.0f);
}

Dungeon::Cell &Dungeon::cell(int x, int y, int z)
{
    transform(x, y, z);
    if (x >= 0 && y >= 0 && z >= 0 && x < m_sizeX && y < m_sizeY && z < m_sizeZ)
        return m_cells[(z * m_sizeY + y) * m_sizeX + x];
    static Cell outside;
    outside.kind = DungeonCellRoom;
    return outside;
}

plasma::Vector<3, float> clampColor(const plasma::Vector<3, float> &color)
{
    plasma::Vector<3, float> result = color;
    if (result[0] < 0.0f)
        result[0] = 0.0f;
    else if (result[0] > 255.0f)
        result[0] = 255.0f;
    if (result[1] < 0.0f)
        result[1] = 0.0f;
    if (result[1] > 255.0f)
        result[1] = 255.0f;
    if (result[2] < 0.0f)
        result[2] = 0.0f;
    if (result[2] > 255.0f)
        result[2] = 255.0f;
    return result;
}

// Blends humidity from the surrounding three-by-three region records at the displaced block column. Each weight falls with the squared distance beyond the nearest record, scaled by 5e-7 and clamped at zero. Missing records supply 0.5. The zone generator stores the result in Field::humidity.
float World::blendedHumidity(int x, int y)
{
    int minimumX = (x - BlocksPerRegion) / BlocksPerRegion;
    int minimumY = (y - BlocksPerRegion) / BlocksPerRegion;
    int maximumX = (x + BlocksPerRegion) / BlocksPerRegion;
    int maximumY = (y + BlocksPerRegion) / BlocksPerRegion;
    plasma::Vector<2, float> offset = landscapeOffset(x, y);
    int offsetX = (int)(x + offset[0]);
    int offsetY = (int)(y + offset[1]);
    RegionRecord *nearest = 0;
    int nearestDistance = 0;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.5f;
            int distance = (int)distanceSquared(record->column, offsetX, offsetY);
            if (!nearest || distance < nearestDistance) {
                nearest = record;
                nearestDistance = distance;
            }
        }
    }
    if (!nearest)
        return 0.5f;
    float weights = 0.0f;
    float value = 0.0f;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.5f;
            int distance = (int)distanceSquared(record->column, offsetX, offsetY);
            float excess = (distance - nearestDistance) * 5e-7f;
            if (excess > 1.0f)
                excess = 1.0f;
            float weight = 1.0f - excess;
            weights += weight;
            value += record->humidity * weight;
        }
    }
    if (weights > 0.0f) {
        float blended = value / weights;
        float result = blended;
        // Low values rise inside records of kind 3.
        Region::Record *square = getRecord(x / BlocksPerSceneryRecord, y / BlocksPerSceneryRecord);
        if (blended < 0.2f && square && square->kind == Region::Record::SceneryForest) {
            float inside = 1.0f - square->shapeDistance(Fixed(x), Fixed(y));
            float weight = 0.0f >= inside ? 0.0f : inside * inside;
            result = weight * 0.3f + blended;
            if (result > 1.0f)
                result = 1.0f;
        }
        return result;
    }
    return 0.5f;
}

// Blends temperature from the surrounding three-by-three region records at the displaced block column. Each weight falls with the squared distance beyond the nearest record, scaled by 5e-7 and clamped at zero. Missing records supply 0.5. The zone generator stores the result in Field::temperature.
float World::blendedTemperature(int x, int y)
{
    int minimumX = (x - BlocksPerRegion) / BlocksPerRegion;
    int minimumY = (y - BlocksPerRegion) / BlocksPerRegion;
    int maximumX = (x + BlocksPerRegion) / BlocksPerRegion;
    int maximumY = (y + BlocksPerRegion) / BlocksPerRegion;
    plasma::Vector<2, float> offset = landscapeOffset(x, y);
    int offsetX = (int)(x + offset[0]);
    int offsetY = (int)(y + offset[1]);
    RegionRecord *nearest = 0;
    int nearestDistance = 0;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.5f;
            int distance = (int)distanceSquared(record->column, offsetX, offsetY);
            if (!nearest || distance < nearestDistance) {
                nearest = record;
                nearestDistance = distance;
            }
        }
    }
    if (!nearest)
        return 0.5f;
    float weights = 0.0f;
    float value = 0.0f;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.5f;
            int distance = (int)distanceSquared(record->column, offsetX, offsetY);
            float excess = (distance - nearestDistance) * 5e-7f;
            if (excess > 1.0f)
                excess = 1.0f;
            float weight = 1.0f - excess;
            weights += weight;
            value += record->temperature * weight;
        }
    }
    if (weights > 0.0f)
        return value / weights;
    return 0.5f;
}

// Joins two rooms (cell positions and sizes) by corridors: along y where they overlap in x, else
// along x where they overlap in y, from the middle of the target room, clamped to the source room,
// sometimes with a second and third corridor beside it.
void Dungeon::connect(const plasma::Vector<3, int> &fromPosition, const plasma::Vector<3, int> &fromSize,
                      const plasma::Vector<3, int> &toPosition, const plasma::Vector<3, int> &toSize)
{
    int fromEndX = fromPosition[0] + fromSize[0];
    if (toPosition[0] < fromEndX && fromPosition[0] < toPosition[0] + toSize[0]) {
        int cx = toPosition[0] + toSize[0] / 2;
        if (cx < fromPosition[0])
            cx = fromPosition[0];
        if (cx >= fromEndX)
            cx = fromEndX - 1;
        if (toPosition[1] < fromPosition[1]) {
            carveCorridor(plasma::Vector<3, int>(cx, fromPosition[1], fromPosition[2]),
                          plasma::Vector<3, int>(cx, toPosition[1] + toSize[1], toPosition[2]));
            if (rand() % 2 && cx > fromPosition[0] && cx > toPosition[0])
                carveCorridor(plasma::Vector<3, int>(cx - 1, fromPosition[1], fromPosition[2]),
                              plasma::Vector<3, int>(cx - 1, toPosition[1] + toSize[1], toPosition[2]));
            if (rand() % 2 && cx < fromPosition[0] + fromSize[0] - 1 && cx < toPosition[0] + toSize[0] - 1)
                carveCorridor(plasma::Vector<3, int>(cx + 1, fromPosition[1], fromPosition[2]),
                              plasma::Vector<3, int>(cx + 1, toPosition[1] + toSize[1], toPosition[2]));
        } else {
            carveCorridor(plasma::Vector<3, int>(cx, fromPosition[1] + fromSize[1] - 1, fromPosition[2]),
                          plasma::Vector<3, int>(cx, toPosition[1] - 1, toPosition[2]));
            if (rand() % 2 && cx > fromPosition[0] && cx > toPosition[0])
                carveCorridor(plasma::Vector<3, int>(cx - 1, fromPosition[1] + fromSize[1] - 1, fromPosition[2]),
                              plasma::Vector<3, int>(cx - 1, toPosition[1] - 1, toPosition[2]));
            if (rand() % 2 && cx < fromPosition[0] + fromSize[0] - 1 && cx < toPosition[0] + toSize[0] - 1)
                carveCorridor(plasma::Vector<3, int>(cx + 1, fromPosition[1] + fromSize[1] - 1, fromPosition[2]),
                              plasma::Vector<3, int>(cx + 1, toPosition[1] - 1, toPosition[2]));
        }
    } else {
        int fromEndY = fromSize[1] + fromPosition[1];
        if (toPosition[1] < fromEndY && fromPosition[1] < toPosition[1] + toSize[1]) {
            int cy = toPosition[1] + toSize[1] / 2;
            if (cy < fromPosition[1])
                cy = fromPosition[1];
            if (cy >= fromEndY)
                cy = fromEndY - 1;
            // The bounds of the third corridors compare y with x extents.
            if (toPosition[0] < fromPosition[0]) {
                carveCorridor(plasma::Vector<3, int>(fromPosition[0], cy, fromPosition[2]),
                              plasma::Vector<3, int>(toPosition[0] + toSize[0], cy, toPosition[2]));
                if (rand() % 2 && cy > fromPosition[1] && cy > toPosition[1])
                    carveCorridor(plasma::Vector<3, int>(fromPosition[0], cy - 1, fromPosition[2]),
                                  plasma::Vector<3, int>(toPosition[0] + toSize[0], cy - 1, toPosition[2]));
                if (rand() % 2 && cy < fromPosition[1] + fromSize[1] - 1 && cy < toPosition[0] + toSize[0] - 1)
                    carveCorridor(plasma::Vector<3, int>(fromPosition[0], cy + 1, fromPosition[2]),
                                  plasma::Vector<3, int>(toPosition[0] + toSize[0], cy + 1, toPosition[2]));
            } else {
                carveCorridor(plasma::Vector<3, int>(fromEndX - 1, cy, fromPosition[2]),
                              plasma::Vector<3, int>(toPosition[0] - 1, cy, toPosition[2]));
                if (rand() % 2 && cy > fromPosition[1] && cy > toPosition[1])
                    carveCorridor(plasma::Vector<3, int>(fromPosition[0] + fromSize[0] - 1, cy - 1, fromPosition[2]),
                                  plasma::Vector<3, int>(toPosition[0] - 1, cy - 1, toPosition[2]));
                if (rand() % 2 && cy < fromPosition[0] + fromSize[0] - 1 && cy < toPosition[0] + toSize[0] - 1)
                    carveCorridor(plasma::Vector<3, int>(fromPosition[0] + fromSize[0] - 1, cy + 1, fromPosition[2]),
                                  plasma::Vector<3, int>(toPosition[0] - 1, cy + 1, toPosition[2]));
            }
        }
    }
}

// Selects the surface block from column humidity, temperature, firelands and height: soil, hot rock, dry sand, shore sand near sea level or snow in cold terrain. River influence and some region kinds fade the sand selection.
Block World::terrainBlock(int x, int y, int z, float humidity, float temperature, Zone *zone)
{
    Block block;
    block.type = BlockGrass;
    plasma::Vector<3, float> color;
    color = soilColor(x, y);
    float river = (riverDistance(x, y, zone) * 10.0f - 0.3f) * 1.5f;
    if (river < 0.0f)
        river = 0.0f;
    else if (river > 1.0f)
        river = 1.0f;
    if (temperature > 0.8f) {
        float t = (temperature - 0.8f) / 0.1f;
        if (t > 1.0f)
            t = 1.0f;
        float n = noise(x * 0.03, y * 0.03);
        if (n > 1.0f)
            n = 1.0f;
        else if (n < 0.0f)
            n = 0.0f;
        float weight = (1.0f - (1.0f - n * n) * (1.0f - n * n)) * 0.8f * t;
        color = rockColor(x, y, z, zone) * weight + color * (1.0f - weight);
    }
    if (humidity < 0.2f && temperature > 0.75f) {
        block.type = BlockSand;
        float sand = (1.0f - humidity / 0.2f) * (temperature - 0.75f) * 4.0f * 10.0f;
        if (sand > 1.0f)
            sand = 1.0f;
        else if (sand < 0.0f)
            sand = 0.0f;
        Region::Record *record = getRecord(x / BlocksPerSceneryRecord, y / BlocksPerSceneryRecord);
        if (record && record->kind == Region::Record::SceneryLake) {
            float s = record->shapeDistance(Fixed(x), Fixed(y)) - 0.5f;
            if (s < 0.0f)
                s = 0.0f;
            else if (s > 1.0f)
                s = 1.0f;
            sand *= 1.0f - (1.0f - s * s) * (1.0f - s * s);
        }
        sand *= river;
        if (sand > 1.0f)
            sand = 1.0f;
        else if (sand < 0.5f)
            block.type = BlockGrass;
        color = sandColor(x, y) * sand + color * (1.0f - sand);
        // Shore noise frequency/offsets, altitude fade width, noise bias and cold suppression cutoff.
    }
    float shore = noise(x * 0.01 + 854.0, y * 0.01 + 985.0) + (15 - z) / 10.0f - 0.5f;
    if (shore > 1.0f)
        shore = 1.0f;
    else if (shore < 0.0f)
        shore = 0.0f;
    shore = 1.0f - (1.0f - shore * shore) * (1.0f - shore * shore);
    if (temperature < 0.2f)
        shore *= temperature / 0.2f;
    if (shore > 0.0f) {
        color = beachColor(x, y) * shore + color * (1.0f - shore);
        block.type = BlockSand;
    }
    if (temperature < 0.3f) {
        block.type = BlockSnow;
        float snow = 1.0f - (temperature - 0.29f) / 0.01f;
        if (snow > 1.0f)
            snow = 1.0f;
        color = snowColor(x, y) * snow + color * (1.0f - snow);
    }
    Field *field = getField(x, y, zone);
    float firelands = field ? field->firelands : blendedFirelands(x, y);
    if (firelands > 0.0f) {
        color = rockColor(x, y, z, zone) * firelands + color * (1.0f - firelands);
        if (firelands > 0.5f)
            block.type = BlockRock;
    }
    block.color = color;
    block.type |= (1 << BlockFlagDestructible);
    return block;
}

// The terrain's surface height at a block column. The base height is the regions' heights
// (RegionRecord::height) weighted by distance from a jittered point, falling off within 4,500
// blocks of the nearest; hills of two broad noises rise on land; three detail noises add bumps,
// flattened along rivers and roads, inside settlements and near the regions' records. A record
// square's scenery then shapes it: a lake below a sunken landscape (kind other than 11), a basin
// (kind 4), walls of a crater (kinds 6 and 7, never below the base height) or a peak (kind 13).
float World::terrainHeight(int x, int y, Zone *zone)
// Broad hill and detail noise frequencies.
{
    float hills1 = (noise(m_heightNoiseOffsets[0] + x * 0.0001, m_heightNoiseOffsets[1] + y * 0.0001) + 1.0f) * 0.5f;
    float hills2 = (noise(m_heightNoiseOffsets[2] + x * 0.0001, m_heightNoiseOffsets[3] + y * 0.0001) + 1.0f) * 0.5f;
    float detail1 = (noise(m_heightNoiseOffsets[4] + x * 0.001, m_heightNoiseOffsets[5] + y * 0.001) + 1.0f) * 0.5f;
    float detail2 = (noise(m_heightNoiseOffsets[6] + x * 0.001, m_heightNoiseOffsets[7] + y * 0.001) + 1.0f) * 0.5f;
    float detail3 = (noise(m_heightNoiseOffsets[8] + x * 0.002, m_heightNoiseOffsets[9] + y * 0.002) + 1.0f) * 0.5f;
    hills1 *= hills1;
    hills2 *= hills2;
    detail1 *= detail1;
    detail2 *= detail2;
    detail3 *= detail3;

    // Rivers flatten the details (a smoothstep of the distance).
    float river = riverDistance(x, y, zone) * 4.0f;
    if (river > 1.0f)
        river = 1.0f;
    float bank = river * 3.0f * river - river * 2.0f * river * river;
    bank *= bank;
    detail1 *= bank;
    detail2 *= bank;
    detail3 *= bank;

    // The regions around the column, nearest first to a jittered point.
    int minX = (x - BlocksPerRegion) / BlocksPerRegion;
    int minY = (y - BlocksPerRegion) / BlocksPerRegion;
    int maxX = (x + BlocksPerRegion) / BlocksPerRegion;
    // Terrain sampling warp: 0.0005 noise with independent channel salts, scaled by three zones.
    int maxY = (y + BlocksPerRegion) / BlocksPerRegion;
    float jitterX = noise(y * 0.0005, 3423.0) * 3.0f * 256.0f;
    float jitterY = noise(x * 0.0005, 23421.0) * 3.0f * 256.0f;
    Fixed blockX = x;
    Fixed blockY = y;
    plasma::Vector<2, Fixed> point(blockX + Fixed(jitterX), blockY + Fixed(jitterY));
    RegionRecord *nearest = 0;
    float nearestDistance = 0.0f;
    for (int i = minX; i <= maxX; i++) {
        for (int j = minY; j <= maxY; j++) {
            RegionRecord *region = getRegionRecord(i, j);
            if (!region)
                continue;
            plasma::Vector<2, float> offset(plasma::Vector<2, Fixed>(region->column[0], region->column[1]) - point);
            float distance = offset[1] * offset[1] + offset[0] * offset[0];
            if (!nearest || distance < nearestDistance) {
                nearest = region;
                nearestDistance = distance;
            }
        }
    }
    float baseHeight = 0.0f;
    float landShare = 1.0f;
    if (nearest) {
        float weights = 0.0f;
        float heights = 0.0f;
        float land = 0.0f;
        for (int i = minX; i <= maxX; i++) {
            for (int j = minY; j <= maxY; j++) {
                RegionRecord *region = getRegionRecord(i, j);
                if (!region)
                    continue;
                plasma::Vector<2, float> offset(plasma::Vector<2, Fixed>(region->column[0], region->column[1]) - point);
                float distance = offset[1] * offset[1] + offset[0] * offset[0];
                float falloff = (distance - nearestDistance) * 5e-8f;
                if (falloff > 1.0f)
                    falloff = 1.0f;
                float weight = 1.0f - falloff;
                weight *= weight;
                weights += weight;
                heights += region->height * weight;
                if (region->height > 0)
                    land += weight;
            }
        }
        baseHeight = heights / weights;
        landShare = land / weights;
    }

    // Hill noise frequency and amplitude.
    float hills =
        (noise(m_heightNoiseOffsets[12] + x * 0.0002, m_heightNoiseOffsets[13] + y * 0.0002) + 1.0f) * 100.0f * hills2;
    hills +=
        (noise(m_heightNoiseOffsets[10] + x * 0.0002, m_heightNoiseOffsets[11] + y * 0.0002) + 1.0f) * 100.0f * hills1;
    float height = hills * landShare + baseHeight;

    // Near the regions' records the finest detail fades out.
    float influence = recordInfluence(x, y);
    if (influence > 0.5f) {
        float t = (influence - 0.5f) * 2.0f;
        if (t > 1.0f)
            t = 1.0f;
        detail3 *= 1.0f - (t * 3.0f * t - t * 2.0f * t * t);
    }
    // Roads flatten the details.
    float road = roadDistance(x, y, zone);
    if (road < 0.02f)
        road = 0.02f;
    road *= 2.0f;
    if (road > 1.0f)
        road = 1.0f;
    float closeness = 1.0f - road;
    float away = 1.0f - closeness * closeness * closeness * closeness;
    detail1 *= away * 0.1f + 0.9f;
    detail2 *= away * 0.1f + 0.9f;
    detail3 *= away * 0.5f + 0.5f;
    // So does a settlement (kind 1), by its shape.
    Region::Record *square = getRecord(x / BlocksPerSceneryRecord, y / BlocksPerSceneryRecord);
    if (square && square->kind == Region::Record::SceneryCity) {
        float inside = 1.0f - square->shapeDistance(blockX, blockY);
        if (inside > 0.0f)
            inside *= inside;
        else
            inside = 0.0f;
        float scale = 1.0f - inside * 0.5f;
        detail1 *= scale;
        detail2 *= scale;
        detail3 *= scale;
        // Detail noise frequencies and height amplitudes.
    }
    float bumps =
        (noise(m_heightNoiseOffsets[16] + x * 0.002, m_heightNoiseOffsets[17] + y * 0.002) + 1.0f) * 50.0f * detail2;
    bumps +=
        (noise(m_heightNoiseOffsets[14] + x * 0.002, m_heightNoiseOffsets[15] + y * 0.002) + 1.0f) * 50.0f * detail1;
    height += bumps + (noise(m_heightNoiseOffsets[18] + x * 0.01, m_heightNoiseOffsets[19] + y * 0.01) + 1.0f) * 20.0f *
                          detail3;

    if (square && square->kind != Region::Record::SceneryNone) {
        // A lake: a sunken landscape lowers the ground around the record's centre (a noisy disc of the
        // record's radius plus 256 blocks).
        RegionRecord *landscape = nearestRegionRecord((int)square->center[0], (int)square->center[1]);
        // Lake outline warp: independent 0.0025 noise channels displace x/y by up to 100 blocks.
        if (landscape && landscape->height < 0 && square->kind != Region::Record::SceneryRock) {
            float dx = (float)(Fixed(noise(x * 0.0025 + 8432984.0, y * 0.0025 + 90493.0) * 100.0f) - square->center[0] +
                               blockX);
            float dy = (float)(Fixed(noise(x * 0.0025 + 3423.0, y * 0.0025 + 112.0) * 100.0f + y) - square->center[1]);
            float t = (dy * dy + dx * dx) / ((square->radius + 256.0f) * (square->radius + 256.0f));
            if (t > 1.0f)
                t = 1.0f;
            t = 1.0f - t;
            if (t > 0.0f) {
                t *= 1.1f;
                if (t > 1.0f)
                    t = 1.0f;
                height -= (t * 3.0f * t - t * 2.0f * t * t) * landscape->height;
            }
        }
        int kind = square->kind;
        if (kind == Region::Record::SceneryLake) {
            // Lake profile inner radius squared and depth offsets.
            float distance = square->shapeDistance(blockX, blockY);
            if (distance <= 0.25f) {
                float t = 1.0f - distance * 4.0f;
                t *= t;
                height = (1.0f - t) * (square->height - 25.0f) + (square->height - 50.0f) * t;
            } else if (distance < 1.0f) {
                float root = sqrt(distance);
                float t = (root - 0.5f) * 2.0f;
                t = 1.0f - t * t;
                height = (square->height - 25.0f - height) * (t * t) + height;
            }
        }
        if (kind == Region::Record::SceneryCanyon || kind == Region::Record::SceneryValley) {
            float distance = square->shapeDistance(blockX, blockY);
            if (distance <= 0.25f) {
                float t = 1.0f - distance * 4.0f;
                // Canyon/valley profile radius squared, rim rise and centre depth.
                t *= t;
                height = (1.0f - t) * (height + 10.0f) + (height - 30.0f) * t;
            } else if (distance < 1.0f) {
                float root = sqrt(distance);
                float t = (root - 0.5f) * 2.0f;
                t = 1.0f - t * t;
                height = t * t * 10.0f + height;
            }
            if (baseHeight > height)
                height = baseHeight;
        }
        if (kind == Region::Record::SceneryPeak) {
            // Peak height and plateau radius.
            float distance = square->shapeDistance(blockX, blockY);
            if (distance <= 0.1f * 0.1f) {
                height += 150.0f;
            } else if (distance < 1.0f) {
                float root = sqrt(distance);
                float t = (root - 0.1f) / 0.9f;
                t = 1.0f - t * t;
                height = t * t * t * t * 150.0f + height;
            }
        }
    }
    return height;
}

// Colors open ground with five noise layers and a vertical shade, blending towards rock in heat, snow in cold and burnt rock in firelands.
plasma::Vector<3, float> World::groundColor(int x, int y, int z, Zone *zone)
// Ground shade channel offsets/frequencies and RGB noise amplitude.
{
    float shade = ((noise(y * 0.1 + 98984.0, z * 0.4 + 8437.0) + noise(x * 0.1, z * 0.4)) * 0.5f + 1.0f) * 0.5f;
    float red = noise(m_groundColorNoiseOffsets[4] + x * 0.01, m_groundColorNoiseOffsets[5] + y * 0.01) * 20.0f;
    // Ground base and shade palette.
    float green = noise(m_groundColorNoiseOffsets[6] + x * 0.01, m_groundColorNoiseOffsets[7] + y * 0.01) * 20.0f;
    // Procedural RGB palette and brightness coefficients for this surface.
    float blue = noise(m_groundColorNoiseOffsets[8] + x * 0.01, m_groundColorNoiseOffsets[9] + y * 0.01) * 20.0f;
    plasma::Vector<3, float> light = plasma::Vector<3, float>(60.0f, 60.0f, 0.0f) * shade;
    float baseRed =
        noise(m_groundColorNoiseOffsets[0] + x * 0.01, m_groundColorNoiseOffsets[1] + y * 0.01) * 20.0f + 180.0f;
    float baseGreen =
        noise(m_groundColorNoiseOffsets[2] + x * 0.01, m_groundColorNoiseOffsets[3] + y * 0.01) * 20.0f + 100.0f;
    plasma::Vector<3, float> color = clampColor(light + plasma::Vector<3, float>(baseRed, baseGreen, 60.0f) +
                                                plasma::Vector<3, float>(red, green, blue));
    float temperature = terrainTemperature(x, y, zone);
    if (temperature > 0.75f) {
        float t = (temperature - 0.75f) * 4.0f;
        if (t < 0.0f)
            temperature = 0.0f; // the code clamps the wrong variable (t is positive here anyway)
        color = rockColor(x, y, z, zone) * t * 0.9f + color * (1.0f - t);
    }
    if (temperature < 0.2f) {
        float t = 1.0f - temperature * 4.0f;
        color = (snowColor(x, y) + rockColor(x, y, z, zone)) * (t * 0.5f) + color * (1.0f - t);
    }
    Field *field = getField(x, y, zone);
    float firelands = field ? field->firelands : blendedFirelands(x, y);
    if (firelands > 0.0f)
        color = rockColor(x, y, z, zone) * firelands + color * (1.0f - firelands);
    return color;
}

namespace {
enum {
    JewelryEquipChanceDenominator = 10
};
enum {
    WarriorWeaponLoadoutCount = 3
};
}

// Dresses and arms a spawn: vendors get clothes and fixed multipliers; other creatures lose their
// weapons and armour, and get the weapons and abilities of their race or, for humanoids, of a
// random class, and armour matching the weapon. Unless keepMultipliers, the race sets the stat
// multipliers, and a tinted enemy is made stronger and gets a random special ability and summons.
// The items' levels are the spawn's, their modifiers random.
void World::equip(Spawn *spawn, bool keepMultipliers)
{
    if (spawn->appearance.appearanceFlags & (1 << AppearanceFlagClassMaster))
        return;
    if (spawn->classType == ClassArchaeologist || spawn->classType == ClassInnkeeper ||
        spawn->classType == ClassItemVendor || spawn->classType == ClassWeaponVendor ||
        spawn->classType == ClassArmorVendor || spawn->classType == ClassAnalyzer || spawn->classType == ClassSmith ||
        spawn->classType == ClassCarpenter || spawn->classType == ClassTailor || spawn->classType == ClassAdapter) {
        // Clothes; the archaeologist's chest piece is a random one of five (both arms are spelled out: the
        // code repeats the stores in each).
        if (spawn->classType == ClassArchaeologist) {
            spawn->equipment.chest.category = ItemCategoryChest;
            spawn->equipment.chest.id = 0;
            spawn->equipment.chest.material = ItemMaterialClothing;
            spawn->equipment.chest.id = rand() % 5 + 2;
        } else {
            spawn->equipment.chest.category = ItemCategoryChest;
            spawn->equipment.chest.id = 0;
            spawn->equipment.chest.material = ItemMaterialClothing;
        }
        spawn->multipliers.damage = 0.1f;
        spawn->multipliers.attackSpeed = 0.3f;
        spawn->multipliers.armor = 10.0f;
        spawn->multipliers.resistance = 10.0f;
        return;
    }

    spawn->equipment.weaponLeft.category = ItemCategoryNone;
    spawn->equipment.weaponLeft.id = WeaponSword;
    spawn->equipment.weaponRight.category = ItemCategoryNone;
    spawn->equipment.weaponRight.id = WeaponSword;
    spawn->equipment.shoulder.category = ItemCategoryNone;
    spawn->equipment.shoulder.id = 0;
    spawn->equipment.chest.category = ItemCategoryNone;
    spawn->equipment.chest.id = 0;
    spawn->equipment.feet.category = ItemCategoryNone;
    spawn->equipment.feet.id = 0;
    spawn->equipment.hands.category = ItemCategoryNone;
    spawn->equipment.hands.id = 0;
    spawn->equipment.neck.category = ItemCategoryNone;
    spawn->equipment.neck.id = 0;

    if (!(spawn->appearance.appearanceFlags & ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagBiteAttack)))) {
        if (spawn->hostilityType == HostilityAttackableObject) {
        } else if (spawn->race == RaceRuneGiant) {
            spawn->equipment.weaponLeft.category = ItemCategoryNone;
            spawn->equipment.weaponLeft.id = WeaponSword;
            spawn->equipment.weaponRight.category = ItemCategoryNone;
            spawn->equipment.weaponRight.id = WeaponSword;
        } else if (spawn->race == RaceLich) {
            spawn->specialSkills.push_back(AbilitySummon);
            spawn->summonAppearanceMode = SummonSelectedRace;
            spawn->summonRaces.push_back(RaceSkeleton);
            spawn->summonRaces.push_back(RaceZombie);
        } else if (spawn->race == RaceTroll || spawn->race == RaceYeti || spawn->race == RaceOgre) {
            if (spawn->race == RaceTroll || spawn->race == RaceYeti) {
                spawn->specialSkills.push_back(AbilityQuake);
                spawn->specialSkills.push_back(Ability93);
            }
            spawn->equipment.weaponRight.category = ItemCategoryWeapon;
            spawn->equipment.weaponRight.id = WeaponGreatmace;
            spawn->equipment.weaponRight.material = ItemMaterialWood;
            spawn->equipment.weaponLeft.category = ItemCategoryNone;
            spawn->equipment.weaponLeft.id = WeaponSword;
        } else if (spawn->race == RaceCyclops) {
            spawn->specialSkills.push_back(AbilityCyclone);
            spawn->specialSkills.push_back(Ability93);
            spawn->equipment.weaponRight.category = ItemCategoryWeapon;
            spawn->equipment.weaponRight.id = WeaponFist;
            spawn->equipment.weaponRight.material = ItemMaterialIron;
            spawn->equipment.weaponLeft.category = ItemCategoryWeapon;
            spawn->equipment.weaponLeft.id = WeaponFist;
            spawn->equipment.weaponLeft.material = ItemMaterialIron;
        } else if (spawn->race == RaceDarkTroll) {
            spawn->specialSkills.push_back(AbilityCyclone);
            spawn->specialSkills.push_back(Ability93);
            spawn->specialSkills.push_back(AbilityQuake);
            spawn->equipment.weaponRight.category = ItemCategoryWeapon;
            spawn->equipment.weaponRight.id = WeaponMace;
            spawn->equipment.weaponRight.material = ItemMaterialBone;
            spawn->equipment.weaponLeft.category = ItemCategoryWeapon;
            spawn->equipment.weaponLeft.id = WeaponMace;
            spawn->equipment.weaponLeft.material = ItemMaterialBone;
        } else if (spawn->race == RaceDjinn) {
            spawn->specialSkills.push_back(AbilityExplosion);
            spawn->equipment.weaponRight.category = ItemCategoryWeapon;
            spawn->equipment.weaponRight.id = WeaponBracelet;
            spawn->equipment.weaponRight.material = ItemMaterialGold;
            spawn->equipment.weaponLeft.category = ItemCategoryWeapon;
            spawn->equipment.weaponLeft.id = WeaponBracelet;
            spawn->equipment.weaponLeft.material = ItemMaterialGold;
        } else if (spawn->race == RaceMinotaur) {
            spawn->equipment.weaponRight.category = ItemCategoryWeapon;
            spawn->equipment.weaponRight.id = WeaponSword;
            spawn->equipment.weaponRight.material = ItemMaterialIron;
            spawn->equipment.weaponLeft.category = ItemCategoryWeapon;
            spawn->equipment.weaponLeft.id = WeaponSword;
            spawn->equipment.weaponLeft.material = ItemMaterialIron;
        } else if (spawn->race == RaceRockling || spawn->race == RaceGolem || spawn->race == RaceSnowGolem ||
                   spawn->race == RaceEmberGolem) {
        } else if (spawn->race == RaceWitch || spawn->race == RaceWizard) {
            spawn->specialSkills.push_back(AbilityFireRay);
            spawn->specialization = Specialization2;
            spawn->equipment.weaponRight.category = ItemCategoryWeapon;
            spawn->equipment.weaponRight.id = rand() % 2 + WeaponStaff;
            spawn->equipment.weaponRight.material = ItemMaterialWood;
            spawn->equipment.weaponLeft.category = ItemCategoryNone;
            spawn->equipment.weaponLeft.id = WeaponSword;
        } else {
            spawn->classType = rand() % (ClassRogue - ClassWarrior + 1) + ClassWarrior;
            if (spawn->race == RaceVampire)
                spawn->classType = ClassRogue;
            switch (spawn->classType) {
            case ClassWarrior:
                spawn->specialization = rand() % 2;
                switch (rand() % WarriorWeaponLoadoutCount) {
                case 0:
                    // A two-handed weapon.
                    spawn->equipment.weaponRight.category = ItemCategoryWeapon;
                    spawn->equipment.weaponRight.id =
                        rand() % (WeaponGreatmace - WeaponGreatsword + 1) + WeaponGreatsword;
                    spawn->equipment.weaponRight.material = ItemMaterialIron;
                    spawn->equipment.weaponLeft.category = ItemCategoryNone;
                    spawn->equipment.weaponLeft.id = WeaponSword;
                    break;
                case 1:
                    // Two one-handed weapons.
                    spawn->equipment.weaponRight.category = ItemCategoryWeapon;
                    spawn->equipment.weaponRight.id = rand() % (WeaponMace - WeaponSword + 1);
                    spawn->equipment.weaponRight.material = ItemMaterialIron;
                    spawn->equipment.weaponLeft.category = ItemCategoryWeapon;
                    spawn->equipment.weaponLeft.id = rand() % (WeaponMace - WeaponSword + 1);
                    spawn->equipment.weaponLeft.material = ItemMaterialIron;
                    break;
                case 2:
                    // A one-handed weapon and a shield.
                    spawn->equipment.weaponRight.category = ItemCategoryWeapon;
                    spawn->equipment.weaponRight.id = rand() % (WeaponMace - WeaponSword + 1);
                    spawn->equipment.weaponRight.material = ItemMaterialIron;
                    spawn->equipment.weaponLeft.category = ItemCategoryWeapon;
                    spawn->equipment.weaponLeft.id = WeaponShield;
                    spawn->equipment.weaponLeft.material = ItemMaterialIron;
                    break;
                }
                break;
            case ClassRanger:
                spawn->specialization = rand() % 2;
                spawn->equipment.weaponRight.category = ItemCategoryWeapon;
                spawn->equipment.weaponRight.id = rand() % (WeaponBoomerang - WeaponBow + 1) + WeaponBow;
                spawn->equipment.weaponRight.material = ItemMaterialWood;
                break;
            case ClassMage:
                spawn->specialization = rand() % 2;
                switch (rand() % 3) {
                case 0:
                    spawn->equipment.weaponRight.category = ItemCategoryWeapon;
                    spawn->equipment.weaponRight.id = WeaponStaff;
                    spawn->equipment.weaponRight.material = ItemMaterialWood;
                    spawn->equipment.weaponLeft.category = ItemCategoryNone;
                    spawn->equipment.weaponLeft.id = WeaponSword;
                    break;
                case 1:
                    spawn->equipment.weaponRight.category = ItemCategoryWeapon;
                    spawn->equipment.weaponRight.id = WeaponWand;
                    spawn->equipment.weaponRight.material = ItemMaterialWood;
                    spawn->equipment.weaponLeft.category = ItemCategoryNone;
                    spawn->equipment.weaponLeft.id = WeaponSword;
                    break;
                case 2:
                    spawn->equipment.weaponRight.category = ItemCategoryWeapon;
                    spawn->equipment.weaponRight.id = WeaponBracelet;
                    spawn->equipment.weaponRight.material = ItemMaterialGold;
                    spawn->equipment.weaponLeft.category = ItemCategoryWeapon;
                    spawn->equipment.weaponLeft.id = WeaponBracelet;
                    spawn->equipment.weaponLeft.material = ItemMaterialGold;
                    break;
                }
                break;
            case ClassRogue:
                spawn->specialization = rand() % 2;
                spawn->equipment.weaponRight.category = ItemCategoryWeapon;
                spawn->equipment.weaponRight.id = rand() % (WeaponLongsword - WeaponDagger + 1) + WeaponDagger;
                spawn->equipment.weaponRight.material = ItemMaterialIron;
                if (spawn->equipment.weaponRight.id == WeaponLongsword) {
                    spawn->equipment.weaponLeft.category = ItemCategoryNone;
                    spawn->equipment.weaponLeft.id = WeaponSword;
                    spawn->equipment.weaponLeft.material = 0;
                } else {
                    // The same weapon in the other hand.
                    spawn->equipment.weaponLeft.category = ItemCategoryWeapon;
                    spawn->equipment.weaponLeft.id = spawn->equipment.weaponRight.id;
                    spawn->equipment.weaponLeft.material = ItemMaterialIron;
                }
                break;
            }

            if (!(spawn->appearance.appearanceFlags & (1 << AppearanceFlagUnarmored))) {
                // Armour of the weapon's kind: cloth for magic weapons, linen for ranged ones, cotton for daggers,
                // fists and longswords.
                int material = ItemMaterialIron;
                if (spawn->equipment.weaponRight.id == WeaponStaff || spawn->equipment.weaponRight.id == WeaponWand ||
                    spawn->equipment.weaponRight.id == WeaponBracelet)
                    material = ItemMaterialSilk;
                else if (spawn->equipment.weaponRight.id == WeaponBow ||
                         spawn->equipment.weaponRight.id == WeaponCrossbow ||
                         spawn->equipment.weaponRight.id == WeaponBoomerang)
                    material = ItemMaterialLinen;
                else if (spawn->equipment.weaponRight.id == WeaponDagger ||
                         spawn->equipment.weaponRight.id == WeaponFist ||
                         spawn->equipment.weaponRight.id == WeaponLongsword)
                    material = ItemMaterialCotton;
                spawn->equipment.chest.category = ItemCategoryChest;
                spawn->equipment.chest.level = spawn->level;
                spawn->equipment.chest.material = material;
                if (rand() % 2) {
                    spawn->equipment.shoulder.category = ItemCategoryShoulder;
                    spawn->equipment.shoulder.level = spawn->level;
                    spawn->equipment.shoulder.material = material;
                }
                if (rand() % 2) {
                    spawn->equipment.hands.category = ItemCategoryHands;
                    spawn->equipment.hands.level = spawn->level;
                    spawn->equipment.hands.material = material;
                }
                if (rand() % 2) {
                    spawn->equipment.feet.category = ItemCategoryFeet;
                    spawn->equipment.feet.level = spawn->level;
                    spawn->equipment.feet.material = material;
                }
                if (rand() % JewelryEquipChanceDenominator == 0) {
                    spawn->equipment.neck.category = ItemCategoryNeck;
                    spawn->equipment.neck.level = spawn->level;
                    spawn->equipment.neck.material = rand() % 2 ? ItemMaterialGold : ItemMaterialSilver;
                }
                if (rand() % 10 == 0) {
                    spawn->equipment.ringRight.category = ItemCategoryRing;
                    spawn->equipment.ringRight.level = spawn->level;
                    spawn->equipment.ringRight.material = rand() % 2 ? ItemMaterialGold : ItemMaterialSilver;
                }
                if (rand() % 10 == 0) {
                    spawn->equipment.ringLeft.category = ItemCategoryRing;
                    spawn->equipment.ringLeft.level = spawn->level;
                    spawn->equipment.ringLeft.material = rand() % 2 ? ItemMaterialGold : ItemMaterialSilver;
                }
            }
        }
    } else {
        // Creatures that attack with their hands or bite carry no weapons.
        spawn->equipment.weaponRight.category = ItemCategoryNone;
        spawn->equipment.weaponRight.id = WeaponSword;
        spawn->equipment.weaponLeft.category = ItemCategoryNone;
        spawn->equipment.weaponLeft.id = WeaponSword;
    }
    spawn->equipment.weaponRight.level = spawn->level;
    spawn->equipment.weaponLeft.level = spawn->level;

    if (!keepMultipliers) {
        Creature::StatMultipliers &multipliers = spawn->multipliers;
        switch (spawn->race) {
        case RaceSeaCrab:
        case RaceTroll:
        case RaceDarkTroll:
        case RaceGolem:
        case RaceEmberGolem:
        case RaceSnowGolem:
        case RaceYeti:
        case RaceCyclops:
        case RaceMammoth:
        case RaceRuneGiant:
            multipliers.hp = 2500.0f;
            multipliers.damage = 6.0f;
            multipliers.armor = 5.0f;
            multipliers.resistance = 5.0f;
            if (spawn->equipment.weaponRight.isTwoHanded())
                multipliers.attackSpeed = 0.75f;
            else
                multipliers.attackSpeed = 0.5f;
            break;
        case RaceSaurian:
            multipliers.attackSpeed = 0.5f;
            multipliers.hp = 2500.0f;
            multipliers.damage = 6.0f;
            multipliers.armor = 10.0f;
            multipliers.resistance = 10.0f;
            break;
        case RaceLich:
            multipliers.hp = 2500.0f;
            multipliers.damage = 5.0f;
            multipliers.armor = 4.0f;
            multipliers.resistance = 5.0f;
            break;
        case RaceImp:
            multipliers.hp = 1000.0f;
            multipliers.damage = 2.0f;
            multipliers.armor = 3.0f;
            multipliers.resistance = 3.0f;
            break;
        case RaceSpitter:
            spawn->classType = ClassMage;
            spawn->specialization = SpecializationWater;
            break;
        case RaceOgre:
        case RaceMinotaur:
            multipliers.hp = 250.0f;
            multipliers.armor = 3.0f;
            multipliers.resistance = 3.0f;
            spawn->specialSkills.push_back(AbilityCyclone);
            break;
        case RaceRockling:
        case RaceBiter:
            multipliers.attackSpeed *= 0.5f;
            multipliers.hp = 250.0f;
            multipliers.damage = 3.0f;
            multipliers.armor = 3.0f;
            multipliers.resistance = 3.0f;
            break;
        case RaceDjinn:
            multipliers.hp = 250.0f;
            multipliers.armor = 3.0f;
            multipliers.resistance = 3.0f;
            spawn->specialSkills.push_back(AbilitySummon);
            break;
        case RaceSkullBull:
            multipliers.hp = 250.0f;
            multipliers.damage = 2.0f;
            spawn->specialSkills.push_back(AbilityHornCharge);
            multipliers.armor = 3.0f;
            multipliers.resistance = 3.0f;
            break;
        case RaceSkeleton:
        case RaceWerewolf:
        case RaceVampire:
            multipliers.hp = 400.0f;
            multipliers.damage = 1.5f;
            multipliers.armor = 3.0f;
            multipliers.resistance = 3.0f;
            spawn->specialSkills.push_back(Ability93);
            break;
        case RaceTurtle:
            multipliers.hp = 200.0f;
            multipliers.damage = 0.5f;
            multipliers.armor = 3.0f;
            multipliers.resistance = 3.0f;
            break;
        }

        if (spawn->appearance.appearanceFlags & (1 << AppearanceFlagTinted)) {
            multipliers.hp *= 15.0f;
            if (multipliers.hp > 10000.0f)
                multipliers.hp = 10000.0f;
            multipliers.damage *= 3.0f;
            multipliers.attackSpeed *= 0.75f;
            multipliers.armor += 2.0f;
            multipliers.resistance += 2.0f;
            std::vector<int> abilities;
            abilities.push_back(AbilitySummon);
            abilities.push_back(Ability93);
            abilities.push_back(AbilityCyclone);
            abilities.push_back(AbilityLava);
            abilities.push_back(AbilityExplosion);
            spawn->specialSkills.push_back(abilities[rand() % abilities.size()]);
            spawn->summonAppearanceMode = rand() % SummonAppearanceModeCount;
            spawn->summonRaces.push_back(RaceSkeleton);
            spawn->summonRaces.push_back(RaceZombie);
        }
    }

    spawn->equipment.weaponLeft.modifier = rand();
    spawn->equipment.weaponRight.modifier = rand();
    spawn->equipment.shoulder.modifier = rand();
    spawn->equipment.chest.modifier = rand();
    spawn->equipment.hands.modifier = rand();
    spawn->equipment.feet.modifier = rand();
}

// Returns the column's humidity, or its blended value when unloaded.
float World::terrainHumidity(int x, int y, Zone *zone)
{
    Field *field = getField(x, y, zone);
    if (field)
        return field->humidity;
    return blendedHumidity(x, y);
}

// Returns the column's temperature, or its blended value when unloaded.
float World::terrainTemperature(int x, int y, Zone *zone)
{
    Field *field = getField(x, y, zone);
    if (field)
        return field->temperature;
    return blendedTemperature(x, y);
}

namespace {
enum {
    ShopStockCountRange = 3
};
}

// Stocks an armour vendor like fillWeaponShop: for each of 30 level steps, one to three pieces
// drawn from every armour piece of the four materials (the shoulder piece listed twice for the
// cloth materials, as in randomItem), an amulet and a ring. The release also calls this
// vendor-stock operation fillArmorShop.
void World::fillArmorShop(Spawn *spawn, int x, int y)
{
    for (int step = 0; step < 30; step++) {
        int minimum = (int)levelFromBonus((float)step / 30.0f);
        int maximum = (int)levelFromBonus(((float)step + 0.99999f) / 30.0f);
        int level = rand() % (maximum - minimum + 1) + minimum;
        if (level < 1)
            level = 1;
        if (step == 0)
            level = 1;

        std::vector<Item> items;
        Item item;
        item.level = level;
        item.material = ItemMaterialIron;
        item.flags = (1 << ItemFlagAdapted);
        item.category = ItemCategoryShoulder;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryChest;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryHands;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryFeet;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.material = ItemMaterialSilk;
        item.category = ItemCategoryShoulder;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryShoulder;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryChest;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryHands;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryFeet;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.material = ItemMaterialLinen;
        item.category = ItemCategoryShoulder;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryShoulder;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryChest;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryHands;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryFeet;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.material = ItemMaterialCotton;
        item.category = ItemCategoryShoulder;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryShoulder;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryChest;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryHands;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryFeet;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryNeck;
        item.material = rand() % 2 ? ItemMaterialGold : ItemMaterialSilver;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.category = ItemCategoryRing;
        item.material = rand() % 2 ? ItemMaterialGold : ItemMaterialSilver;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);

        int count = rand() % ShopStockCountRange + 1;
        for (int i = 0; i < count; i++) {
            if (items.empty())
                break;
            int index = rand() % items.size();
            spawn->inventory.add(items[index]);
            items.erase(items.begin() + index);
        }
    }
}

namespace {
enum {
    LampRarityStockChanceScale = 10
};
enum {
    ShopPetFoodChoiceCount = 6
};
enum {
    ShopFormulaCountRange = 3
};
enum {
    RarityFirstUpgradeChanceDenominator = 100
};
enum {
    RaritySecondUpgradeChanceDenominator = 1000
};
enum {
    RarityThirdUpgradeChanceDenominator = 10000
};
}

// Stocks an item vendor like fillWeaponShop's level steps: the first step adds a glass flask, a
// lamp (and rarer lamps by chance), the two special items and one pet's food; every step adds up
// to two formulas of a weapon or armour piece and, half the time, a bomb. The release also calls
// this operation fillGeneralShop.
void World::fillGeneralShop(Spawn *spawn, int x, int y)
{
    for (int step = 0; step < 30; step++) {
        int minimum = (int)levelFromBonus((float)step / 30.0f);
        int maximum = (int)levelFromBonus(((float)step + 0.99999f) / 30.0f);
        int level = rand() % (maximum - minimum + 1) + minimum;
        if (level < 1)
            level = 1;
        if (step == 0) {
            level = 1;
            Item item;
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemFlask;
            item.material = ItemMaterialGlass;
            item.level = level;
            spawn->inventory.add(item, 0);
            item.category = ItemCategoryLamp;
            item.id = LampLamp;
            item.material = ItemMaterialIron;
            item.level = level;
            spawn->inventory.add(item, 0);
            for (int rarity = 1; rarity < ItemRarityYellow + 1; rarity++) {
                if (rand() % (rarity * LampRarityStockChanceScale) == 0) {
                    item.category = ItemCategoryLamp;
                    item.id = LampLamp;
                    item.material = ItemMaterialIron;
                    item.level = level;
                    item.rarity = rarity;
                    spawn->inventory.add(item, 0);
                }
            }
            item.rarity = ItemRarityWhite;
            item.material = ItemMaterialWood;
            item.category = ItemCategorySpecial;
            item.id = SpecialItemHangGlider;
            spawn->inventory.add(item, 0);
            item.id = SpecialItemBoat;
            item.material = ItemMaterialWood;
            spawn->inventory.add(item, 0);
            // Food for one of the pets.
            item.category = ItemCategoryPetFood;
            item.material = 0;
            item.level = level;
            switch (rand() % ShopPetFoodChoiceCount) {
            case 0:
                item.id = RaceSheep;
                break;
            case 1:
                item.id = RaceBunny;
                break;
            case 2:
                item.id = RaceCollie;
                break;
            case 3:
                item.id = RaceTerrier;
                break;
            case 4:
                item.id = RaceCat;
                break;
            case 5:
                item.id = RaceMole;
                break;
            }
            spawn->inventory.add(item, 0);
        }
        int count = rand() % ShopFormulaCountRange;
        for (int i = 0; i < count; i++) {
            int rarity = rand() % 2;
            if (rand() % RarityFirstUpgradeChanceDenominator == 0)
                rarity++;
            if (rand() % RaritySecondUpgradeChanceDenominator == 0)
                rarity++;
            if (rand() % RarityThirdUpgradeChanceDenominator == 0)
                rarity++;
            if (rarity > ItemRarityYellow)
                rarity = ItemRarityYellow;
            Item formula;
            formula = rand() % 2 ? randomWeapon(level, rarity, -1) : randomArmor(level, rarity, -1);
            formula.formulaCategory = formula.category;
            formula.category = ItemCategoryFormula;
            spawn->inventory.add(formula, 0);
        }
        if (rand() % 2) {
            Item bomb;
            bomb.category = ItemCategoryConsumable;
            bomb.id = ConsumableBomb;
            bomb.level = level;
            spawn->inventory.add(bomb, 0);
        }
    }
}

// Gives a creature one tab of ten random items for its level and quality (the dungeons' wall
// creatures).
void World::fillInventory(Inventory *inventory, int level, int quality)
{
    inventory->m_inventoryTabs.clear();
    inventory->m_inventoryTabs.resize(1);
    inventory->m_inventoryTabs[InventoryTabEquipment].resize(10);
    for (int i = 0; i < (int)inventory->m_inventoryTabs[InventoryTabEquipment].size(); i++) {
        inventory->m_inventoryTabs[InventoryTabEquipment][i].quantity = 1;
        inventory->m_inventoryTabs[InventoryTabEquipment][i].item = randomInventoryItem(level, quality);
    }
}

// Stocks a weapon vendor : for each of 30 level steps, one to three weapons drawn from a list of
// every kind at a level of that step, each of its own random rarity. The zone coordinates are not
// used; the release calls it fillWeaponShop.
void World::fillWeaponShop(Spawn *spawn, int x, int y)
{
    for (int step = 0; step < 30; step++) {
        int minimum = (int)levelFromBonus((float)step / 30.0f);
        int maximum = (int)levelFromBonus(((float)step + 0.99999f) / 30.0f);
        int level = rand() % (maximum - minimum + 1) + minimum;
        if (level < 1)
            level = 1;
        if (step == 0)
            level = 1;

        std::vector<Item> items;
        Item item;
        item.level = level;
        item.material = ItemMaterialIron;
        item.flags = (1 << ItemFlagAdapted);
        item.category = ItemCategoryWeapon;
        item.id = rand() % (WeaponMace - WeaponSword + 1);
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.material = ItemMaterialIron;
        item.id = rand() % (WeaponGreatmace - WeaponGreatsword + 1) + WeaponGreatsword;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.id = WeaponShield;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.id = WeaponDagger;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.id = WeaponLongsword;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.id = WeaponFist;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.material = ItemMaterialWood;
        item.id = WeaponBow;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        // The crossbow is drawn but never appended: the boomerang overwrites it.
        item.id = WeaponCrossbow;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        item.id = WeaponBoomerang;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.id = WeaponStaff;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.id = WeaponWand;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);
        item.id = WeaponBracelet;
        item.material = rand() % 2 ? ItemMaterialGold : ItemMaterialSilver;
        item.rarity = randomRarity(ItemRarityWhite, false);
        item.modifier = rand();
        items.push_back(item);

        int count = rand() % ShopStockCountRange + 1;
        for (int i = 0; i < count; i++) {
            if (items.empty())
                break;
            int index = rand() % items.size();
            spawn->inventory.add(items[index]);
            items.erase(items.begin() + index);
        }
    }
}

plasma::Vector<2, int> World::findNearestRegion(int x, int y)
{
    int minX = (x - BlocksPerRegion) / BlocksPerRegion;
    int minY = (y - BlocksPerRegion) / BlocksPerRegion;
    int maxX = (x + BlocksPerRegion) / BlocksPerRegion;
    int maxY = (y + BlocksPerRegion) / BlocksPerRegion;
    plasma::Vector<2, float> offset = landscapeOffset(x, y);
    int positionX = (int)(x + offset[0]);
    int positionY = (int)(y + offset[1]);
    plasma::Vector<2, int> region(-1, -1);
    RegionRecord *nearest = 0;
    int nearestDistance = 0;
    for (; minX <= maxX; minX++) {
        for (int j = minY; j <= maxY; j++) {
            RegionRecord *record = getRegionRecord(minX, j);
            if (!record)
                continue;
            int distance = (int)distanceSquared(record->column, positionX, positionY);
            if (!nearest || distance < nearestDistance) {
                nearest = record;
                nearestDistance = distance;
                region = plasma::Vector<2, int>(minX, j);
            }
        }
    }
    return region;
}

Region *World::regionAtColumn(int x, int y)
{
    int minX = (x - BlocksPerRegion) / BlocksPerRegion;
    int minY = (y - BlocksPerRegion) / BlocksPerRegion;
    int maxX = (x + BlocksPerRegion) / BlocksPerRegion;
    int maxY = (y + BlocksPerRegion) / BlocksPerRegion;
    plasma::Vector<2, float> offset = landscapeOffset(x, y);
    int positionX = (int)(x + offset[0]);
    int positionY = (int)(y + offset[1]);
    Region *region = 0;
    RegionRecord *nearest = 0;
    int nearestDistance = 0;
    for (; minX <= maxX; minX++) {
        for (int j = minY; j <= maxY; j++) {
            RegionRecord *record = getRegionRecord(minX, j);
            if (!record)
                continue;
            int distance = (int)distanceSquared(record->column, positionX, positionY);
            if (!nearest || distance < nearestDistance) {
                region = m_regions[minX][j];
                nearest = record;
                nearestDistance = distance;
            }
        }
    }
    return region;
}

// The box of a marker's voxels whose lowest corner is (x, y, z); defined after the zone generator.
bool markerBounds(Sprite *sprite, int x, int y, int z, int rotation, plasma::Vector<3, int> *low,
                  plasma::Vector<3, int> *high);

// Fits an object to the box of a sprite's marker voxels: centred on the box across, standing on
// its bottom, as large as the box (turned once when the box is deeper than wide), and turned on.
// False unless (x, y, z) is the box's corner, so each marker yields one object.
static bool objectFromVoxels(Sprite *sprite, const plasma::Vector<3, int> &origin, int rotation, int x, int y, int z,
                             Zone::Object *object)
{
    plasma::Vector<3, int> low;
    plasma::Vector<3, int> high;
    if (!markerBounds(sprite, x, y, z, rotation, &low, &high))
        return false;
    plasma::Vector<3, Fixed> center = plasma::Vector<3, Fixed>(low + high) * Fixed(0.5);
    object->position[0] = center[0];
    object->position[1] = center[1];
    object->position[2] = Fixed(low[2]);
    object->position += plasma::Vector<3, Fixed>(origin);
    int width = high[0] - low[0];
    int depth = high[1] - low[1];
    if (width > depth) {
        object->rotation = 0;
        object->size = plasma::Vector<3, float>((float)width, (float)depth, (float)(high[2] - low[2]));
    } else {
        object->rotation = 1;
        object->size = plasma::Vector<3, float>((float)depth, (float)width, (float)(high[2] - low[2]));
    }
    object->active = StaticObjectActive;
    return true;
}

void World::fillInteriorBox(int x, int y, int z, const plasma::Vector<3, int> &size,
                            const plasma::Vector<3, unsigned char> &color, Zone *zone)
{
    for (int i = 0; i < size[0]; i++) {
        for (int j = 0; j < size[1]; j++) {
            for (int k = size[2] - 1; k >= 0; k--) {
                int bx = x + i;
                int by = y + j;
                int bz = z + k;
                setBlock(bx, by, bz, Block(color, BlockBuilding | (1 << BlockFlagInterior)), zone);
            }
        }
    }
}

// BuildRock's sibling with a gentler outline: a noisy elliptic cylinder of radii 2 * sizeX and 2 *
// sizeY around (x, y), up to about z + sizeZ plus noise times sizeZ. The top block gets the
// terrain's surface block; below it the ground colour turns into rockColor's stone over 20 blocks
// starting 8 blocks above the column's base, all of type ground. Unlike buildRock, any submerged
// block in the shape (water too) prevents it.
void World::buildArch(int x, int y, int z, int sizeX, int sizeY, int sizeZ, Zone *zone)
{
    for (int bx = x - sizeX * 2; bx <= x + sizeX * 2; bx++) {
        for (int by = y - sizeY * 2; by <= y + sizeY * 2; by++) {
            Field *field = getField(bx, by, zone);
            if (!field)
                continue;
            // Rock/arch outline noise: independent channels deform the section and roughen its stone.
            int bottom = field->m_baseZ;
            for (int bz = (int)(noise(bx * 0.01 + 4394.0, by * 0.01 + 8974.0) * sizeZ + (z + sizeZ)); bz >= bottom;
                 // Arch outline noise frequency (distinct from the documented stone transition rate).
                 bz--) {
                float u = noise(by * 0.05, bz * 0.05) * 0.4f + (float)(bx - x) / sizeX;
                float v = noise(bx * 0.05 + 4374.0, bz * 0.05 + 9898.0) * 0.4f + (float)(by - y) / sizeY;
                if (u * u + v * v <= 1.0f && (getBlock(bx, by, bz, zone).type & (1 << BlockFlagUnderwater)))
                    return;
            }
        }
    }
    for (int bx = x - sizeX * 2; bx <= x + sizeX * 2; bx++) {
        for (int by = y - sizeY * 2; by <= y + sizeY * 2; by++) {
            int top = (int)(noise(bx * 0.01 + 4394.0, by * 0.01 + 8974.0) * sizeZ + (z + sizeZ));
            Field *field = getField(bx, by, zone);
            if (!field)
                continue;
            int bottom = field->m_baseZ;
            for (int bz = top; bz >= bottom; bz--) {
                const Block &block = getBlock(bx, by, bz, zone);
                int type = block.blockType();
                if ((type == BlockAir || type == BlockWater || type == BlockGrass || type == BlockSand ||
                     type == BlockGround) &&
                    !block.isInterior()) {
                    float u = noise(by * 0.05, bz * 0.02) * 0.5f + (float)(bx - x) / sizeX;
                    float v = noise(bx * 0.05 + 4374.0, bz * 0.02 + 9898.0) * 0.5f + (float)(by - y) / sizeY;
                    if (u * u + v * v <= 1.0f) {
                        Block rock;
                        if (bz == top) {
                            rock = terrainBlock(bx, by, bz, terrainHumidity(bx, by, zone),
                                                terrainTemperature(bx, by, zone), zone);
                        } else {
                            float stone = (bz - bottom - 8) * 0.05f;
                            if (stone < 0.0f)
                                stone = 0.0f;
                            else if (stone > 1.0f)
                                stone = 1.0f;
                            rock.color =
                                groundColor(bx, by, bz, zone) * (1.0f - stone) + rockColor(bx, by, bz, zone) * stone;
                            rock.type = BlockGround;
                        }
                        setBlock(bx, by, bz, rock, zone);
                    }
                }
            }
        }
    }
}

// A rock: a noisy elliptic cylinder of radii 2 * sizeX and 2 * sizeY around (x, y), from the
// ground up to about z + sizeZ, which replaces air, water, grass, sand and ground (not interiors):
// the top block gets the terrain's surface block, the rest stone of rockColor's colour. Nothing is
// built where the shape meets a submerged block that is not water.
void World::buildRock(int x, int y, int z, int sizeX, int sizeY, int sizeZ, Zone *zone)
{
    for (int bx = x - sizeX * 2; bx <= x + sizeX * 2; bx++) {
        for (int by = y - sizeY * 2; by <= y + sizeY * 2; by++) {
            Field *field = getField(bx, by, zone);
            if (!field)
                continue;
            // Rock/arch outline noise: independent channels deform the section and roughen its stone.
            int bottom = field->m_baseZ;
            for (int bz = (int)(noise(bx * 0.01 + 4394.0, by * 0.01 + 8974.0) * 20.0f + (z + sizeZ)); bz >= bottom;
                 bz--) {
                float u = noise(by * 0.05, bz * 0.05) * (10.0f / sizeX) + (float)(bx - x) / sizeX;
                float v = noise(bx * 0.05 + 4374.0, bz * 0.05 + 9898.0) * (10.0f / sizeY) + (float)(by - y) / sizeY;
                if (u * u + v * v <= 1.0f && getBlock(bx, by, bz, zone).blockType() != BlockWater &&
                    (getBlock(bx, by, bz, zone).type & (1 << BlockFlagUnderwater)))
                    return;
            }
        }
    }
    for (int bx = x - sizeX * 2; bx <= x + sizeX * 2; bx++) {
        for (int by = y - sizeY * 2; by <= y + sizeY * 2; by++) {
            int top = (int)(noise(bx * 0.01 + 4394.0, by * 0.01 + 8974.0) * 20.0f + (z + sizeZ));
            Field *field = getField(bx, by, zone);
            if (!field)
                continue;
            int bottom = field->m_baseZ;
            for (int bz = top; bz >= bottom; bz--) {
                const Block &block = getBlock(bx, by, bz, zone);
                int type = block.blockType();
                if ((type == BlockAir || type == BlockWater || type == BlockGrass || type == BlockSand ||
                     type == BlockGround) &&
                    !block.isInterior()) {
                    float u = noise(by * 0.05, bz * 0.05) * (10.0f / sizeX) + (float)(bx - x) / sizeX;
                    float v = noise(bx * 0.05 + 4374.0, bz * 0.05 + 9898.0) * (10.0f / sizeY) + (float)(by - y) / sizeY;
                    if (u * u + v * v <= 1.0f) {
                        Block rock;
                        if (bz == top) {
                            rock = terrainBlock(bx, by, bz, terrainHumidity(bx, by, zone),
                                                terrainTemperature(bx, by, zone), zone);
                        } else {
                            rock.color = rockColor(bx, by, bz, zone);
                            rock.type = BlockBuilding;
                        }
                        setBlock(bx, by, bz, rock, zone);
                    }
                }
            }
        }
    }
}

namespace {
enum {
    DungeonRoomLayoutCount = 3
};
enum {
    DungeonCorridorLayoutCount = 3
};
enum {
    RaisedRoomPlatformChanceDenominator = 3
};
enum {
    RaisedDungeonColumnChanceDenominator = 6
};
enum {
    RaisedDungeonColumnHeightRange = 3
};
enum {
    WetDungeonEntranceScorePenalty = 100
};
enum {
    TempleEntranceDepthBias = 50
};
enum {
    DungeonSpikeTrapChanceDenominator = 40
};
enum {
    DungeonSpikeTrapPhasePeriod = 4000
};
enum {
    DungeonCeilingPlantChanceDenominator = 3
};
enum {
    DungeonWallFurnishingChanceDenominator = 8
};
enum {
    ChestInventoryTabCount = 5
};
enum {
    ShelfRareLootChanceDenominator = 6
};
enum {
    ShelfCommonLootChoiceCount = 4
};
enum {
    ShelfIngredientChoiceCount = 6
};
enum {
    GuardPotionCountRange = 4
};
enum {
    SpiralStairPhaseMask = 7
};
enum {
    PatrolLeaderMinimumRoomArea = 6
};
enum {
    PatrolFollowerCountRange = 3
};
enum {
    WallCobwebChanceDenominator = 3
};
enum {
    WallFurnishingMinimumLateralOffset = 3
};
enum {
    WallFurnishingLateralRange = 4
};
}

// Lays out the dungeon of a region cell in a zone: rooms on a 3 x 3 x 3 grid of 7-block steps
// joined by corridors (one of five layouts by variant), carved into a 22 x 22 x 22 Dungeon, then
// built as blocks at (x, y) with doors, lamps, traps, chests and the creatures that guard it.
void World::generateDungeon(Zone *zone, Region *region, int x, int y, int variant, bool large)
{
    std::vector<Dungeon::Style> styles;
    std::vector<int> races;
    int dungeonRarity = zone->cell.rarity;
    int level = zone->cell.level;
    switch (variant) {
    default: {
        races.push_back(RaceOrcMale);
        races.push_back(RaceOrcFemale);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceOgre);
        style.followerRaces.push_back(RaceCollie);
        styles.push_back(style);
        break;
    }
    case Region::Record::DungeonPyramid: {
        races.push_back(RaceHorus);
        races.push_back(RaceAnubis);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceSkeleton);
        style.guardRaces.push_back(RaceDjinn);
        style.followerRaces.push_back(RaceMosquito);
        style.followerRaces.push_back(RaceCat);
        styles.push_back(style);
        break;
    }
    case Region::Record::DungeonPalace: {
        races.push_back(RaceHumanMale);
        races.push_back(RaceHumanFemale);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceHumanMale);
        style.guardRaces.push_back(RaceHumanFemale);
        style.followerRaces.push_back(RaceCollie);
        styles.push_back(style);
        break;
    }
    case Region::Record::DungeonRuins:
    case Region::Record::DungeonCatacombs: {
        races.push_back(RaceUndeadMale);
        races.push_back(RaceUndeadFemale);
        {
            Dungeon::Style style;
            style.guardRaces.push_back(RaceUndeadMale);
            style.guardRaces.push_back(RaceUndeadFemale);
            style.followerRaces.push_back(RaceZombie);
            styles.push_back(style);
        }
        switch (rand() % DungeonRoomLayoutCount) {
        case 0: {
            Dungeon::Style style;
            style.guardRaces.push_back(RaceVampire);
            styles.push_back(style);
            break;
        }
        case 1: {
            Dungeon::Style style;
            style.guardRaces.push_back(RaceWerewolf);
            styles.push_back(style);
            break;
        }
        case 2: {
            Dungeon::Style style;
            style.guardRaces.push_back(RaceSkeleton);
            styles.push_back(style);
            break;
        }
        }
        break;
    }
    }

    // The 3 x 3 x 3 room grid: each room gets a random offset and size.
    plasma::Vector<3, int> entranceCell(0, 0, 0);
    Dungeon::Room rooms[3][3][3];
    for (int rx = 0; rx < 3; rx++) {
        for (int ry = 0; ry < 3; ry++) {
            // Dungeon room lattice: random size and cell offsets for each room before corridor linking.
            for (int rz = 0; rz < 3; rz++) {
                // The original draws z, y, x in that order for both vectors.
                rooms[rx][ry][rz].size = plasma::Vector<3, int>(rand() % 3 - 1, rand() % 3 - 1, rand() % 3);
                rooms[rx][ry][rz].offset = plasma::Vector<3, int>(rand() % 2, rand() % 2, rand() % 3 - 1);
            }
        }
    }
    // Which rooms exist and the corridors between them; the first link starts at the entrance room.
    std::list<Dungeon::Link> links;
    if (large) {
        rooms[1][1][1].used = true;
        rooms[1][2][1].used = true;
        rooms[0][1][1].used = true;
        rooms[2][1][1].used = true;
        rooms[0][0][1].used = true;
        rooms[2][0][1].used = true;
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 1, 1), plasma::Vector<3, int>(1, 2, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 1, 1), plasma::Vector<3, int>(0, 1, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 1, 1), plasma::Vector<3, int>(2, 1, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 1), plasma::Vector<3, int>(0, 0, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 1, 1), plasma::Vector<3, int>(2, 0, 1)));
    } else if (variant == Region::Record::DungeonTemple || variant == Region::Record::DungeonPyramid) {
        rand();
        rooms[1][1][2].used = true;
        rooms[1][1][2].noPatrol = true;
        rooms[1][1][2].fixedSize = true;
        rooms[1][2][1].used = true;
        rooms[2][2][1].used = true;
        rooms[2][1][1].used = true;
        rooms[2][0][1].used = true;
        rooms[0][2][1].used = true;
        rooms[0][1][1].used = rand() % 2 == 0;
        rooms[0][0][0].used = true;
        rooms[0][1][0].used = true;
        rooms[0][2][0].used = rand() % 2 == 0;
        rooms[1][1][0].used = true;
        rooms[1][1][0].guard = true;
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 1, 2), plasma::Vector<3, int>(1, 2, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 2, 1), plasma::Vector<3, int>(0, 2, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 2, 1), plasma::Vector<3, int>(2, 2, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 2, 1), plasma::Vector<3, int>(0, 1, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 2, 1), plasma::Vector<3, int>(2, 1, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 1, 1), plasma::Vector<3, int>(2, 0, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 0, 1), plasma::Vector<3, int>(0, 0, 0)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 0, 0), plasma::Vector<3, int>(0, 1, 0)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 0), plasma::Vector<3, int>(0, 2, 0)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 0), plasma::Vector<3, int>(1, 1, 0)));
    } else if (variant == Region::Record::DungeonCatacombs) {
        rand();
        rooms[1][0][2].used = true;
        rooms[1][2][1].used = true;
        rooms[2][2][1].used = true;
        rooms[2][1][1].used = true;
        rooms[2][0][1].used = true;
        rooms[0][2][1].used = true;
        rooms[0][1][1].used = rand() % 2 == 0;
        rooms[0][0][0].used = true;
        rooms[0][1][0].used = true;
        rooms[0][2][0].used = rand() % 2 == 0;
        rooms[1][1][0].used = true;
        rooms[1][1][0].guard = true;
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 0, 2), plasma::Vector<3, int>(1, 2, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 2, 1), plasma::Vector<3, int>(0, 2, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 2, 1), plasma::Vector<3, int>(2, 2, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 2, 1), plasma::Vector<3, int>(0, 1, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 2, 1), plasma::Vector<3, int>(2, 1, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 1, 1), plasma::Vector<3, int>(2, 0, 1)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 0, 1), plasma::Vector<3, int>(0, 0, 0)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 0, 0), plasma::Vector<3, int>(0, 1, 0)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 0), plasma::Vector<3, int>(0, 2, 0)));
        links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 0), plasma::Vector<3, int>(1, 1, 0)));
    } else {
        rooms[1][0][1].used = true;
        switch (rand() % DungeonCorridorLayoutCount) {
        case 1:
            rooms[2][0][1].used = rand() % 2 == 0;
            rooms[1][1][1].used = true;
            rooms[0][1][1].used = true;
            rooms[0][0][1].used = rand() % 2 == 0;
            rooms[0][2][1].used = true;
            rooms[1][2][1].used = true;
            rooms[2][2][1].used = true;
            rooms[2][2][1].guard = true;
            rooms[2][1][1].used = rand() % 2 == 0;
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 0, 1), plasma::Vector<3, int>(1, 1, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 0, 1), plasma::Vector<3, int>(2, 0, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 1, 1), plasma::Vector<3, int>(0, 1, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 1), plasma::Vector<3, int>(0, 0, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 1), plasma::Vector<3, int>(0, 2, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 2, 1), plasma::Vector<3, int>(1, 2, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 2, 1), plasma::Vector<3, int>(2, 2, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 2, 1), plasma::Vector<3, int>(2, 1, 1)));
            break;
        case 2:
            rooms[2][0][1].used = rand() % 2 == 0;
            rooms[0][0][1].used = true;
            rooms[0][1][1].used = true;
            rooms[0][2][1].used = rand() % 2 == 0;
            rooms[1][1][1].used = true;
            rooms[2][1][1].used = true;
            rooms[2][2][1].used = rand() % 2 == 0;
            rooms[1][2][1].used = true;
            rooms[1][2][1].guard = true;
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 0, 1), plasma::Vector<3, int>(0, 0, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 0, 1), plasma::Vector<3, int>(2, 0, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 0, 1), plasma::Vector<3, int>(2, 1, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 0, 1), plasma::Vector<3, int>(0, 1, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 1), plasma::Vector<3, int>(1, 1, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 1), plasma::Vector<3, int>(0, 2, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 1, 1), plasma::Vector<3, int>(1, 2, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 1, 1), plasma::Vector<3, int>(2, 1, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 1, 1), plasma::Vector<3, int>(2, 2, 1)));
            break;
        default:
            rooms[2][0][1].used = rand() % 2 == 0;
            rooms[0][0][1].used = true;
            rooms[0][1][1].used = true;
            rooms[0][2][1].used = rand() % 2 == 0;
            rooms[1][1][1].used = true;
            rooms[1][2][1].used = rand() % 2 == 0;
            rooms[2][1][1].used = true;
            rooms[2][2][1].used = true;
            rooms[2][2][1].guard = true;
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 0, 1), plasma::Vector<3, int>(0, 0, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 0, 1), plasma::Vector<3, int>(2, 0, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 0, 1), plasma::Vector<3, int>(0, 1, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 1), plasma::Vector<3, int>(1, 1, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(0, 1, 1), plasma::Vector<3, int>(0, 2, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 1, 1), plasma::Vector<3, int>(1, 2, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(1, 1, 1), plasma::Vector<3, int>(2, 1, 1)));
            links.push_back(Dungeon::Link(plasma::Vector<3, int>(2, 1, 1), plasma::Vector<3, int>(2, 2, 1)));
            break;
        }
    }

    // Carve the rooms and the corridors into the cell box.
    Dungeon dungeon(22, 22, 22);
    for (int rx = 0; rx < 21; rx += 7) {
        for (int ry = 0; ry < 21; ry += 7) {
            for (int rz = 0; rz < 21; rz += 7) {
                Dungeon::Room &room = rooms[rx / 7][ry / 7][rz / 7];
                if (!room.used)
                    continue;
                if (room.fixedSize) {
                    room.offset = plasma::Vector<3, int>(0, 0, 0);
                    room.size = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
                    // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                }
                // Object layout: per-axis dimensions and placement offsets for this model.
                plasma::Vector<3, int> position = plasma::Vector<3, int>(rx + 2, ry + 2, rz + 3) + room.offset;
                plasma::Vector<3, int> size = plasma::Vector<3, int>(3, 3, 1) + room.size;
                dungeon.addRoom(position, size,
                                !room.fixedSize && !room.guard && rand() % RaisedRoomPlatformChanceDenominator == 0);
                if (room.guard)
                    dungeon.cell(position[0] + size[0] / 2, position[1] + size[1] / 2, position[2] + size[2] / 2)
                        .flags |= (1 << DungeonCellFlagGuard);
            }
        }
    }
    for (std::list<Dungeon::Link>::iterator it = links.begin(); it != links.end(); ++it) {
        Dungeon::Room &from = rooms[it->from[0]][it->from[1]][it->from[2]];
        Dungeon::Room &to = rooms[it->to[0]][it->to[1]][it->to[2]];
        if (!from.used || !to.used)
            // Object layout: per-axis dimensions and placement offsets for this model.
            continue;
        // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
        plasma::Vector<3, int> toSize = plasma::Vector<3, int>(3, 3, 1) + to.size;
        // Object layout: per-axis dimensions and placement offsets for this model.
        plasma::Vector<3, int> toPosition =
            plasma::Vector<3, int>(it->to[0] * 7 + 2, it->to[1] * 7 + 2, it->to[2] * 7 + 3) + to.offset;
        plasma::Vector<3, int> fromSize = plasma::Vector<3, int>(3, 3, 1) + from.size;
        // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
        plasma::Vector<3, int> fromPosition =
            plasma::Vector<3, int>(it->from[0] * 7 + 2, it->from[1] * 7 + 2, it->from[2] * 7 + 3) + from.offset;
        dungeon.connect(fromPosition, fromSize, toPosition, toSize);
    }
    // The entrance: in the first link's room.
    Dungeon::Link &first = links.front();
    Dungeon::Room entrance = rooms[first.from[0]][first.from[1]][first.from[2]];
    // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
    dungeon
        .cell(first.from[0] * 7 + 2 + entrance.offset[0], first.from[1] * 7 + 1 + entrance.offset[1],
              first.from[2] * 7 + 3 + entrance.offset[2])
        .kind = DungeonCellEntrance;
    int rotation = rand() % 4;
    dungeon.m_rotation = rotation;
    dungeon.m_reflectY = rand() % 2 == 0;
    bool palace = variant == Region::Record::DungeonPalace;

    // Dungeon roof/wall/floor palette by variant.
    // Block colours and lighting by variant.
    // The original draws b, g, r in that order for every colour of this function.
    plasma::Vector<3, unsigned char> roofColor(rand() % 50 + 20, rand() % 50 + 20, rand() % 50 + 20);
    int brightness = rand() % 50;
    if (variant == Region::Record::DungeonPalace)
        brightness = rand() % 50 + 100;
    plasma::Vector<3, unsigned char> wallColor(rand() % 50 + 50 + brightness, rand() % 50 + 50 + brightness,
                                               rand() % 50 + 50 + brightness);
    plasma::Vector<3, unsigned char> floorColor(rand() % 50 + 50 + brightness, rand() % 50 + 50 + brightness,
                                                rand() % 50 + 50 + brightness);
    // The id of the category 18 items on shelves (0, or 1 in caves). Dungeon plant light palette.
    int shelfItemId = CandleItemCandle;
    // The light of the green glowing plants.
    plasma::Vector<3, float> plantColor(0.6f, 0.5f, 0.1f);
    // How rough the dungeon's stone fills are.
    float roughness = 0.005f;
    switch (variant) {
    case Region::Record::DungeonRuins:
    case Region::Record::DungeonCatacombs:
        // Procedural RGB palette and brightness coefficients for this surface.
        shelfItemId = CandleItemUndead;
        plantColor = plasma::Vector<3, float>(0.0f, 0.5f, 0.1f);
        wallColor = plasma::Vector<3, unsigned char>(rand() % 20 + 50, rand() % 50 + 50, rand() % 50 + 50);
        floorColor = plasma::Vector<3, unsigned char>(rand() % 20 + 50, rand() % 50 + 50, rand() % 50 + 50);
        roughness = 0.0015f;
        break;
    case Region::Record::DungeonTemple:
        wallColor = plasma::Vector<3, unsigned char>(rand() % 50 + 100, rand() % 50 + 80, rand() % 50 + 20);
        floorColor = plasma::Vector<3, unsigned char>(rand() % 50 + 100, rand() % 50 + 80, rand() % 50 + 20);
        roughness = 0.0025f;
        break;
    case Region::Record::DungeonPyramid:
        wallColor = plasma::Vector<3, unsigned char>(255, 210, 130);
        floorColor = plasma::Vector<3, unsigned char>(200, 180, 80);
        break;
    }

    // The stair sprite, and steps of 2..4 cells above the floor of some columns in the stone layouts.
    Sprite *stairs;
    switch (variant) {
    case Region::Record::DungeonTemple:
        stairs = m_spriteManager.sprite(SpriteTempleArc);
        break;
    case Region::Record::DungeonPyramid:
        stairs = m_spriteManager.sprite(SpriteTempleArc);
        break;
    default:
        stairs = m_spriteManager.sprite(SpriteCastleArc);
        break;
    }
    if (variant == Region::Record::DungeonCastle || variant == Region::Record::DungeonPalace ||
        variant == Region::Record::DungeonRuins || variant == Region::Record::DungeonCatacombs) {
        for (int cx = 0; cx < dungeon.sizeX(); cx++) {
            for (int cy = 0; cy < dungeon.sizeY(); cy++) {
                int height = 1;
                if (variant != Region::Record::DungeonCatacombs && rand() % RaisedDungeonColumnChanceDenominator == 0)
                    height = rand() % RaisedDungeonColumnHeightRange + 2;
                for (int cz = dungeon.m_sizeZ - 1; cz >= 0; cz--) {
                    if (dungeon.cell(cx, cy, cz).kind != DungeonCellEntrance &&
                        dungeon.cell(cx, cy, cz).kind != DungeonCellEmpty) {
                        int top = cz + height;
                        if (dungeon.m_sizeZ - 1 < top)
                            top = dungeon.m_sizeZ - 1;
                        for (int cz2 = cz + 1; cz2 <= top; cz2++)
                            dungeon.cell(cx, cy, cz2).kind = DungeonCellStairColumn;
                        break;
                    }
                }
            }
        }
    }

    // Try the four rotations: keep the one whose entrance lies lowest under the ground, preferring dry
    // ground.
    int bestRotation = -1;
    bool bestWet = false;
    int bestDepth;
    for (int attempt = 0; attempt < 4; attempt++) {
        rotation = (rotation + 1) % 4;
        dungeon.m_rotation = rotation;
        plasma::Vector<2, int> entranceColumn(0, 0);
        int entranceZ = 0;
        int width = dungeon.sizeX();
        for (int cz = dungeon.m_sizeZ - 1; cz >= 0; cz--) {
            for (int cx = 0; cx < width; cx++) {
                int depth = dungeon.sizeY();
                for (int cy = 0; cy < depth; cy++) {
                    if (dungeon.cell(cx, cy, cz).kind == DungeonCellEntrance) {
                        entranceColumn = plasma::Vector<2, int>(cx, cy);
                        entranceZ = cz;
                    }
                }
            }
        }
        int offsetX = entranceColumn[0] * 10;
        // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
        int offsetY = entranceColumn[1] * 10;
        Field *column = getField(x + 5 + offsetX, y + 5 + offsetY, zone);
        if (!column)
            return;
        // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
        int ground = column->m_baseZ + column->m_size;
        while (!(getBlock(x + 5 + offsetX, y + 5 + offsetY, ground, zone).type & BlockTypeMask))
            ground--;
        ground++;
        bool wet = false;
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10; j++) {
                // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                for (int k = 0; k < 10; k++) {
                    int type = getBlock(x + offsetX + i, y + offsetY + j, ground - 5 + k, zone).type & ~BlockFlagMask;
                    if (type == BlockWater || type == BlockWet)
                        wet = true;
                }
            }
        }
        int depth = ground + (wet ? WetDungeonEntranceScorePenalty : 0) - entranceZ * 10;
        if (bestRotation < 0 || depth < bestDepth) {
            bestDepth = depth;
            bestRotation = rotation;
            entranceCell = plasma::Vector<3, int>(entranceColumn[0], entranceColumn[1], entranceZ);
            bestWet = wet;
        }
    }
    if (variant == Region::Record::DungeonTemple || variant == Region::Record::DungeonPyramid)
        bestDepth += TempleEntranceDepthBias;
    if (bestWet)
        bestDepth -= 100;
    if (entranceCell[2] * 10 + bestDepth < 0)
        bestDepth = -(entranceCell[2] * 10);
    if (bestRotation < 0)
        bestRotation = 0;
    // Random carpet and border palette.
    dungeon.m_rotation = bestRotation;
    plasma::Vector<3, unsigned char> carpetColor(rand() % 180 + 50, rand() % 180 + 50, rand() % 180 + 50);
    plasma::Vector<3, unsigned char> carpetBorderColor(rand() % 180 + 50, rand() % 180 + 50, rand() % 180 + 50);
    switch (variant) {
    case Region::Record::DungeonCastle:
    case Region::Record::DungeonPalace:
        // A draw whose result is not used.
        rand();
        break;
    }

    // Build each column of cells as blocks, 10 x 10 blocks per cell, from the top down.
    for (int cx = 0; cx < dungeon.sizeX(); cx++) {
        for (int cy = 0; cy < dungeon.sizeY(); cy++) {
            int baseX = x + cx * 10;
            int baseY = y + cy * 10;
            bool started = false;
            int topDepth = 0;
            Field *column = getField(baseX, baseY, zone);
            int lowest = column->m_baseZ + column->m_size;
            for (int i = 0; i < 10; i++) {
                for (int j = 0; j < 10; j++) {
                    Field *field = getField(baseX + i, baseY + j, zone);
                    int top = field->m_baseZ + field->m_size;
                    if (top < lowest)
                        lowest = top;
                }
            }
            bool aboveEntrance = false;
            for (int cz = dungeon.m_sizeZ - 1; cz >= 0; cz--) {
                int type = dungeon.cell(cx, cy, cz).kind;
                if (type == DungeonCellEmpty)
                    continue;
                if (!started) {
                    topDepth = bestDepth + (cz + 1) * 10;
                    if (type == DungeonCellEntrance)
                        topDepth -= 10;
                }
                if (type == DungeonCellEntrance) {
                    aboveEntrance = true;
                    // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                } else {
                    if (!started && bestDepth + cz * 10 + 5 > lowest) {
                        // The top cell reaches above the ground: walls around it. Dungeon layout: cell-relative shell,
                        // battlement, roof and stepped temple profile.
                        if (variant == Region::Record::DungeonCatacombs) {
                            fillBox(baseX, baseY, bestDepth + cz * 10 + 11, plasma::Vector<3, int>(10, 10, 3),
                                    wallColor, 0.0f, zone, 0);
                        } else if (variant == Region::Record::DungeonCastle ||
                                   variant == Region::Record::DungeonPalace ||
                                   variant == Region::Record::DungeonRuins) {
                            // Battlements on the open sides.
                            if (dungeon.isFlat(cx - 1, cy, cz) && dungeon.isFlat(cx - 1, cy, cz + 1)) {
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                int top = bestDepth + (cz + 1) * 10;
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                fillBox(baseX - 3, baseY - 3, top, plasma::Vector<3, int>(2, 16, 3), wallColor, 0.0f,
                                        zone, 0);
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                for (int k = 0, by = baseY - 2; k < 8; k++, by += 2) {
                                    int above = getBlock(baseX - 3, by, top + 3, zone).type & ~BlockFlagMask;
                                    // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                    if (above == BlockAir || above == BlockWater)
                                        // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                        setBlock(
                                            baseX - 3, by, top + 3,
                                            Block(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior)),
                                            zone);
                                    fillBox(baseX - 3, by - 1, top + 3, plasma::Vector<3, int>(1, 1, 1), wallColor,
                                            0.0f, zone, 0);
                                }
                            }
                            if (dungeon.isFlat(cx + 1, cy, cz) && dungeon.isFlat(cx + 1, cy, cz + 1)) {
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                int top = bestDepth + (cz + 1) * 10;
                                fillBox(x + (cx + 1) * 10 + 1, baseY - 3, top, plasma::Vector<3, int>(2, 16, 3),
                                        wallColor, 0.0f, zone, 0);
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                int bx = x + (cx + 1) * 10 + 2;
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                for (int k = 0, by = baseY - 1; k < 8; k++, by += 2) {
                                    int above = getBlock(bx, by, top + 3, zone).type & ~BlockFlagMask;
                                    // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                    if (above == BlockAir || above == BlockWater)
                                        // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                        setBlock(
                                            bx, by, top + 3,
                                            Block(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior)),
                                            zone);
                                    fillBox(bx, by - 1, top + 3, plasma::Vector<3, int>(1, 1, 1), wallColor, 0.0f, zone,
                                            0);
                                }
                            }
                            if (dungeon.isFlat(cx, cy - 1, cz) && dungeon.isFlat(cx, cy - 1, cz + 1)) {
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                int top = bestDepth + (cz + 1) * 10;
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                fillBox(baseX - 3, baseY - 3, top, plasma::Vector<3, int>(16, 2, 3), wallColor, 0.0f,
                                        zone, 0);
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                for (int k = 0, bx = baseX - 2; k < 8; k++, bx += 2) {
                                    int above = getBlock(bx, baseY - 3, top + 3, zone).type & ~BlockFlagMask;
                                    // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                    if (above == BlockAir || above == BlockWater)
                                        // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                        setBlock(
                                            bx, baseY - 3, top + 3,
                                            Block(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior)),
                                            zone);
                                    fillBox(bx - 1, baseY - 3, top + 3, plasma::Vector<3, int>(1, 1, 1), wallColor,
                                            0.0f, zone, 0);
                                }
                            }
                            if (dungeon.isFlat(cx, cy + 1, cz) && dungeon.isFlat(cx, cy + 1, cz + 1)) {
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                int top = bestDepth + (cz + 1) * 10;
                                fillBox(baseX - 3, y + (cy + 1) * 10 + 1, top, plasma::Vector<3, int>(16, 2, 3),
                                        wallColor, 0.0f, zone, 0);
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                int by = y + (cy + 1) * 10 + 2;
                                for (int k = 0, bx = baseX - 1; k < 8; k++, bx += 2) {
                                    int above = getBlock(bx, by, top + 3, zone).type & ~BlockFlagMask;
                                    if (above == BlockAir || above == BlockWater)
                                        // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                        setBlock(
                                            bx, by, top + 3,
                                            Block(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior)),
                                            zone);
                                    fillBox(bx - 1, by, top + 3, plasma::Vector<3, int>(1, 1, 1), wallColor, 0.0f, zone,
                                            0);
                                }
                            }
                            // A palace's free-standing cell gets a stepped roof. Dungeon layout: cell-relative shell,
                            // battlement, roof and stepped temple profile.
                            if (palace && dungeon.isFlat(cx - 1, cy, cz) && dungeon.isFlat(cx + 1, cy, cz) &&
                                dungeon.isFlat(cx, cy - 1, cz) && dungeon.isFlat(cx, cy + 1, cz)) {
                                // Object layout: per-axis dimensions and placement offsets for this model.
                                fillBox(baseX - 2, baseY - 2, bestDepth + (cz + 1) * 10,
                                        plasma::Vector<3, int>(14, 14, 4), roofColor, 0.0f, zone, 0);
                                for (int size = 14, z = bestDepth + cz * 10 + 14, bx = baseX - 2, by = baseY - 2;
                                     size > 0; size -= 2, z += 2, bx++, by++)
                                    fillBox(bx, by, z, plasma::Vector<3, int>(size, size, 2), roofColor, 0.0f, zone, 0);
                            }
                            // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                            if (dungeon.isFlat(cx - 1, cy, cz) && dungeon.isFlat(cx + 1, cy, cz) &&
                                dungeon.isFlat(cx, cy - 1, cz) && dungeon.isFlat(cx, cy + 1, cz)) {
                                // The ground height at the cell's centre (not used further).
                                Field *center = getField(baseX + 5, baseY + 5, zone);
                                if (center) {
                                    int ground = center->m_size + center->m_baseZ;
                                    // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                    while (true) {
                                        int type = getBlock(baseX + 5, baseY + 5, ground, zone).type & ~BlockFlagMask;
                                        if (type != BlockAir && type != BlockWater)
                                            break;
                                        ground--;
                                    }
                                }
                            }
                        }
                        // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                    }
                    // The cell's shell.
                    fillBox(baseX - 2, baseY - 2, bestDepth + cz * 10 - 2, plasma::Vector<3, int>(14, 14, 14),
                            wallColor, roughness, zone, 0);
                }
                started = true;
            }
            if (started || variant == Region::Record::DungeonTemple || variant == Region::Record::DungeonPyramid) {
                Field *field = getField(baseX, baseY, zone);
                if (field) {
                    // The lowest column base around the cell. Dungeon layout: cell-relative shell, battlement, roof
                    // and stepped temple profile.
                    int bottom = field->m_baseZ;
                    for (int i = -2; i < 12; i++) {
                        for (int j = -2; j < 12; j++) {
                            Field *neighbour = getField(baseX + i, baseY + j, zone);
                            if (neighbour && neighbour->m_baseZ < bottom)
                                bottom = neighbour->m_baseZ;
                        }
                    }
                    // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                    if (variant == Region::Record::DungeonCastle || variant == Region::Record::DungeonPalace ||
                        variant == Region::Record::DungeonCatacombs || variant == Region::Record::DungeonRuins) {
                        // The foundation below the top cell.
                        fillBox(baseX - 2, baseY - 2, bottom, plasma::Vector<3, int>(14, 14, topDepth - bottom),
                                wallColor, roughness, zone, aboveEntrance ? (1 << BlockFlagUnderwater) : 0);
                    } else if (variant == Region::Record::DungeonTemple) {
                        // A stepped temple: each ring around the centre one cell lower.
                        plasma::Vector<2, int> center(dungeon.sizeX() / 2 - 1, dungeon.sizeY() / 2 - 1);
                        int distanceX = abs(cx - center[0]);
                        int distanceY = abs(cy - center[1]);
                        int distance = distanceX > distanceY ? distanceX : distanceY;
                        int step = dungeon.sizeX() / 2 - distance - 1;
                        if (cx == center[0] && cy == center[1])
                            // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                            step--;
                        int top = bestDepth + (entranceCell[2] - 5 + step) * 10;
                        if (distanceX <= 1 && distanceY <= 1)
                            // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                            top += 2;
                        fillBox(baseX - 2, baseY - 2, bottom, plasma::Vector<3, int>(14, 14, top - bottom - 4),
                                wallColor, roughness, zone, 0);
                        // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                        if (top - bottom > 0) {
                            // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                            fillBox(baseX - 1, baseY - 1, top - 4, plasma::Vector<3, int>(12, 12, 3), wallColor, 0.0f,
                                    zone, 0);
                            fillBox(baseX, baseY, top - 1, plasma::Vector<3, int>(10, 10, 3), wallColor, 0.0f, zone, 0);
                            // Ground blocks on the rim where the noise says so. Dungeon layout: cell-relative shell,
                            // battlement, roof and stepped temple profile.
                            for (int i = -2; i < 12; i++) {
                                for (int j = -2, by = baseY - 2; j < 12; j++, by++) {
                                    // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                    int z = top + 2;
                                    // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                    if (i == -2 || i == 11 || j == -2 || j == 11)
                                        z -= 6;
                                    // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                    else if (i == -1 || i == 10 || j == -1 || j == 10)
                                        // Dungeon exterior shell/battlement/roof/temple profile dimension: if (noise((baseX + i) * 0.02,
                                        // by * 0.02) > 0.0f) {.
                                        z -= 3;
                                    if (noise((baseX + i) * 0.02, by * 0.02) > 0.0f) {
                                        const Block &block = getBlock(baseX + i, by, z, zone);
                                        if (!(block.type & BlockTypeMask) || block.blockType() == BlockWater)
                                            setBlock(baseX + i, by, z,
                                                     terrainBlock(baseX + i, by, z,
                                                                  terrainHumidity(baseX + i, by, zone),
                                                                  terrainTemperature(baseX + i, by, zone), zone),
                                                     zone);
                                    }
                                }
                            }
                        }
                        // Stairs up the middle of each side.
                        if (abs(cx - center[0]) == 0 && abs(cy - center[1]) > 1) {
                            if (cy < center[1]) {
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                for (int k = -1, by = baseY - 1; k < 10; k++, by++)
                                    fillBox(baseX - 2, by, top, plasma::Vector<3, int>(14, 1, k + 3), wallColor,
                                            roughness, zone, 0);
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                for (int k = -1, by = baseY - 1; k < 10; k++, by++)
                                    fillBox(baseX - 2, by, top, plasma::Vector<3, int>(2, 1, k + 5), wallColor,
                                            roughness, zone, 0);
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                for (int k = -1, by = baseY - 1; k < 10; k++, by++)
                                    fillBox(x + cx * 10 + 10, by, top, plasma::Vector<3, int>(2, 1, k + 5), wallColor,
                                            roughness, zone, 0);
                            } else if (cy > center[1]) {
                                for (int k = -1, by = baseY + 10; k < 10; k++, by--)
                                    fillBox(baseX - 2, by, top, plasma::Vector<3, int>(14, 1, k + 3), wallColor,
                                            roughness, zone, 0);
                                for (int k = -1, by = baseY + 10; k < 10; k++, by--)
                                    fillBox(baseX - 2, by, top, plasma::Vector<3, int>(2, 1, k + 5), wallColor,
                                            roughness, zone, 0);
                                for (int k = -1, by = baseY + 10; k < 10; k++, by--)
                                    fillBox(x + cx * 10 + 10, by, top, plasma::Vector<3, int>(2, 1, k + 5), wallColor,
                                            roughness, zone, 0);
                            }
                        }
                        if (abs(cy - center[1]) == 0 && abs(cx - center[0]) > 1) {
                            if (cx < center[0]) {
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                for (int k = -1, bx = baseX - 1; k < 10; k++, bx++)
                                    fillBox(bx, baseY - 2, top, plasma::Vector<3, int>(1, 14, k + 3), wallColor,
                                            roughness, zone, 0);
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                for (int k = -1, bx = baseX - 1; k < 10; k++, bx++)
                                    fillBox(bx, baseY - 2, top, plasma::Vector<3, int>(1, 2, k + 5), wallColor,
                                            roughness, zone, 0);
                                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                                for (int k = -1, bx = baseX - 1; k < 10; k++, bx++)
                                    fillBox(bx, y + cy * 10 + 10, top, plasma::Vector<3, int>(1, 2, k + 5), wallColor,
                                            roughness, zone, 0);
                            } else if (cx > center[0]) {
                                for (int k = -1, bx = baseX + 10; k < 10; k++, bx--)
                                    fillBox(bx, baseY - 2, top, plasma::Vector<3, int>(1, 14, k + 3), wallColor,
                                            roughness, zone, 0);
                                for (int k = -1, bx = baseX + 10; k < 10; k++, bx--)
                                    fillBox(bx, baseY - 2, top, plasma::Vector<3, int>(1, 2, k + 5), wallColor,
                                            roughness, zone, 0);
                                for (int k = -1, bx = baseX + 10; k < 10; k++, bx--)
                                    fillBox(bx, y + cy * 10 + 10, top, plasma::Vector<3, int>(1, 2, k + 5), wallColor,
                                            roughness, zone, 0);
                            }
                        }
                    }
                }
            }
        }
    }
    if (variant == Region::Record::DungeonPyramid) {
        // A pyramid of block columns over the whole box.
        int length = dungeon.sizeY() * 10;
        // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
        int extent = dungeon.sizeX() * 10;
        plasma::Vector<2, int> apex(x + extent / 2 - 5, y + length / 2 - 5);
        for (int bx = x; bx < x + extent; bx++) {
            for (int by = y; by < y + length; by++) {
                Field *field = getField(bx, by, zone);
                if (!field)
                    continue;
                int base = field->getBaseZ();
                int distance = abs(by - apex[1]);
                if (abs(bx - apex[0]) > distance)
                    distance = abs(bx - apex[0]);
                // Dungeon layout: cell-relative shell, battlement, roof and stepped temple profile.
                fillBox(
                    bx, by, base,
                    plasma::Vector<3, int>(1, 1, entranceCell[2] * 10 - distance - 51 + extent / 2 + bestDepth - base),
                    wallColor, roughness, zone, 0);
            }
        }
        // The ground height at the apex (not used further).
        Field *top = getField(apex[0], apex[1], zone);
        int ground = top->getBaseZ() + top->size();
        while (!getBlock(apex[0], apex[1], ground, zone).isSolid())
            ground--;
    }

    // Furnish the cells: entrances, openings, bridges, lamps, chests and traps. Constructed and
    // destroyed, never used.
    std::list<plasma::Vector<3, int>> unusedPositions;
    int width = dungeon.sizeX();
    int depth = dungeon.sizeY();
    for (int cx = 0; cx < width; cx++) {
        int height = dungeon.sizeZ();
        for (int cy = 0; cy < depth; cy++) {
            for (int cz = 0; cz < height; cz++) {
                Dungeon::Cell &cell = dungeon.cell(cx, cy, cz);
                if (cell.kind != DungeonCellCorridor) {
                    if (cell.kind != DungeonCellEntrance)
                        continue;
                    // The entrance's record.
                    Zone::GenerationMarker record;
                    // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                    record.kind = GenerationMarkerEntrance;
                    record.position = plasma::Vector<3, Fixed>(x + cx * 10 + 5, y + cy * 10 + 5, bestDepth + cz * 10);
                    zone->m_generationMarkers.push_back(record);
                }
                if (variant == Region::Record::DungeonCastle || variant == Region::Record::DungeonPalace ||
                    variant == Region::Record::DungeonTemple || variant == Region::Record::DungeonPyramid ||
                    variant == Region::Record::DungeonCatacombs || variant == Region::Record::DungeonRuins)
                    cutBox(x + cx * 10, y + cy * 10, bestDepth + cz * 10, plasma::Vector<3, int>(10, 10, 10), zone,
                           variant == Region::Record::DungeonTemple || variant == Region::Record::DungeonCatacombs ||
                               variant == Region::Record::DungeonRuins);
                if (cell.kind == DungeonCellEntrance) {
                    // Constructed and not used.
                    Zone::Object entrance;
                }
                // Dungeon layout: cell-relative window and stair cuts.
                if ((variant == Region::Record::DungeonCastle || variant == Region::Record::DungeonPalace ||
                     variant == Region::Record::DungeonRuins) &&
                    cell.kind != DungeonCellEntrance && !(cell.flags & (1 << DungeonCellFlagStairsUp))) {
                    // Openings (object type 4) in walls that face nothing.
                    int px = x + cx * 10 + 5;
                    int pz = bestDepth + cz * 10 + 2;
                    // Dungeon layout: cell-relative window and stair cuts.
                    if (rand() % 2 && dungeon.cell(cx, cy - 1, cz).kind == DungeonCellEmpty &&
                        !getBlock(px, y + cy * 10 - 3, pz, zone).isSolid()) {
                        cutBox(px - 1, y + cy * 10 - 2, pz, plasma::Vector<3, int>(2, 2, 4), zone, false);
                        Zone::Object opening;
                        // Object layout: per-axis dimensions and placement offsets for this model.
                        opening.type = StaticObjectCastleWindow;
                        opening.size = plasma::Vector<3, float>(2.0f, 0.2f, 4.0f);
                        opening.rotation = 0;
                        opening.position = plasma::Vector<3, Fixed>(px, y + cy * 10 - 1, pz);
                        zone->m_objects.push_back(opening);
                    }
                    // Dungeon layout: cell-relative window and stair cuts.
                    if (rand() % 2 && dungeon.cell(cx, cy + 1, cz).kind == DungeonCellEmpty &&
                        !getBlock(px, y + (cy + 1) * 10 + 3, pz, zone).isSolid()) {
                        cutBox(px - 1, y + (cy + 1) * 10, pz, plasma::Vector<3, int>(2, 2, 4), zone, false);
                        Zone::Object opening;
                        opening.type = StaticObjectCastleWindow;
                        opening.size = plasma::Vector<3, float>(2.0f, 0.2f, 4.0f);
                        opening.rotation = 2;
                        opening.position = plasma::Vector<3, Fixed>(px, y + (cy + 1) * 10 + 1, pz);
                        zone->m_objects.push_back(opening);
                        // Dungeon layout: cell-relative window and stair cuts.
                    }
                    int py = y + cy * 10 + 5;
                    // Dungeon layout: cell-relative window and stair cuts.
                    if (rand() % 2 && dungeon.cell(cx - 1, cy, cz).kind == DungeonCellEmpty &&
                        !getBlock(x + cx * 10 - 3, py, pz, zone).isSolid()) {
                        cutBox(x + cx * 10 - 2, py - 1, pz, plasma::Vector<3, int>(2, 2, 4), zone, false);
                        Zone::Object opening;
                        opening.type = StaticObjectCastleWindow;
                        // Dungeon layout: cell-relative window and stair cuts.
                        opening.size = plasma::Vector<3, float>(2.0f, 0.2f, 4.0f);
                        opening.rotation = 3;
                        opening.position = plasma::Vector<3, Fixed>(x + cx * 10 - 1, py, pz);
                        zone->m_objects.push_back(opening);
                    }
                    // Dungeon layout: cell-relative window and stair cuts.
                    if (rand() % 2 && dungeon.cell(cx + 1, cy, cz).kind == DungeonCellEmpty &&
                        !getBlock(x + (cx + 1) * 10 + 3, py, pz, zone).isSolid()) {
                        cutBox(x + (cx + 1) * 10, py - 1, pz, plasma::Vector<3, int>(2, 2, 4), zone, false);
                        Zone::Object opening;
                        opening.type = StaticObjectCastleWindow;
                        opening.size = plasma::Vector<3, float>(2.0f, 0.2f, 4.0f);
                        opening.rotation = 1;
                        opening.position = plasma::Vector<3, Fixed>(x + (cx + 1) * 10 + 1, py, pz);
                        zone->m_objects.push_back(opening);
                    }
                }
                if ((variant == Region::Record::DungeonCastle || variant == Region::Record::DungeonPalace ||
                     variant == Region::Record::DungeonTemple || variant == Region::Record::DungeonPyramid ||
                     variant == Region::Record::DungeonCatacombs || variant == Region::Record::DungeonRuins) &&
                    (cell.flags & (1 << DungeonCellFlagStairsUp))) {
                    // Stairs up to the next level, towards the open side. Dungeon layout: cell-relative window and
                    // stair cuts.
                    if (dungeon.cell(cx + 1, cy, cz).kind != DungeonCellCorridor &&
                        dungeon.cell(cx - 1, cy, cz).kind == DungeonCellCorridor &&
                        dungeon.cell(cx + 1, cy, cz + 1).kind == DungeonCellCorridor) {
                        for (int step = 11, bx = x + cx * 10 - 1, bz = bestDepth + cz * 10 - 1; step > 0;
                             step--, bx++, bz++)
                            fillBox(bx, y + cy * 10, bz, plasma::Vector<3, int>(step, 10, 1), floorColor, roughness,
                                    zone, 0);
                    }
                    // Dungeon layout: cell-relative window and stair cuts.
                    if (dungeon.cell(cx - 1, cy, cz).kind != DungeonCellCorridor &&
                        dungeon.cell(cx + 1, cy, cz).kind == DungeonCellCorridor &&
                        dungeon.cell(cx - 1, cy, cz + 1).kind == DungeonCellCorridor) {
                        for (int step = 11, bz = bestDepth + cz * 10 - 1; step > 0; step--, bz++)
                            fillBox(x + cx * 10, y + cy * 10, bz, plasma::Vector<3, int>(step, 10, 1), floorColor,
                                    roughness, zone, 0);
                    }
                    if (dungeon.cell(cx, cy + 1, cz).kind != DungeonCellCorridor &&
                        dungeon.cell(cx, cy - 1, cz).kind == DungeonCellCorridor &&
                        dungeon.cell(cx, cy + 1, cz + 1).kind == DungeonCellCorridor) {
                        for (int step = 11, by = y + cy * 10 - 1, bz = bestDepth + cz * 10 - 1; step > 0;
                             step--, by++, bz++)
                            fillBox(x + cx * 10, by, bz, plasma::Vector<3, int>(10, step, 1), floorColor, roughness,
                                    zone, 0);
                    }
                    if (dungeon.cell(cx, cy - 1, cz).kind != DungeonCellCorridor &&
                        dungeon.cell(cx, cy + 1, cz).kind == DungeonCellCorridor &&
                        dungeon.cell(cx, cy - 1, cz + 1).kind == DungeonCellCorridor) {
                        for (int step = 11, bz = bestDepth + cz * 10 - 1; step > 0; step--, bz++)
                            fillBox(x + cx * 10, y + cy * 10, bz, plasma::Vector<3, int>(10, step, 1), floorColor,
                                    roughness, zone, 0);
                    }
                }
                if (variant == Region::Record::DungeonTemple && cell.kind == DungeonCellEntrance)
                    continue;
                if (dungeon.cell(cx, cy, cz - 1).kind != DungeonCellCorridor) {
                    if (variant == Region::Record::DungeonCastle || variant == Region::Record::DungeonPalace ||
                        variant == Region::Record::DungeonTemple || variant == Region::Record::DungeonPyramid ||
                        variant == Region::Record::DungeonCatacombs || variant == Region::Record::DungeonRuins) {
                        // The floor.
                        int bx = x + cx * 10;
                        int by = y + cy * 10;
                        int bz = bestDepth + cz * 10;
                        fillBox(bx, by, bz, plasma::Vector<3, int>(10, 10, 1), floorColor, roughness, zone, 0);
                        if ((variant == Region::Record::DungeonCastle || variant == Region::Record::DungeonPalace) &&
                            (cell.flags & (1 << DungeonCellFlagCarpet))) {
                            // A carpet over the marked cells, with a border where the marked area ends.
                            int right = bx + 10;
                            int top = by + 10;
                            bool borderWest = false;
                            bool borderEast = false;
                            bool borderSouth = false;
                            bool borderNorth = false;
                            if (!(dungeon.cell(cx - 1, cy, cz).flags & (1 << DungeonCellFlagCarpet))) {
                                bx += 2;
                                borderWest = true;
                            }
                            if (!(dungeon.cell(cx + 1, cy, cz).flags & (1 << DungeonCellFlagCarpet))) {
                                right -= 2;
                                borderEast = true;
                            }
                            if (!(dungeon.cell(cx, cy - 1, cz).flags & (1 << DungeonCellFlagCarpet))) {
                                by += 2;
                                borderSouth = true;
                            }
                            if (!(dungeon.cell(cx, cy + 1, cz).flags & (1 << DungeonCellFlagCarpet))) {
                                top -= 2;
                                borderNorth = true;
                            }
                            int sizeX = right - bx;
                            int sizeY = top - by;
                            fillInteriorBox(bx, by, bz, plasma::Vector<3, int>(sizeX, sizeY, 1), carpetColor, zone);
                            if (borderWest)
                                fillInteriorBox(bx, by, bz, plasma::Vector<3, int>(1, sizeY, 1), carpetBorderColor,
                                                zone);
                            if (borderEast)
                                fillInteriorBox(right - 1, by, bz, plasma::Vector<3, int>(1, sizeY, 1),
                                                carpetBorderColor, zone);
                            if (borderSouth)
                                fillInteriorBox(bx, by, bz, plasma::Vector<3, int>(sizeX, 1, 1), carpetBorderColor,
                                                zone);
                            if (borderNorth)
                                fillInteriorBox(bx, top - 1, bz, plasma::Vector<3, int>(sizeX, 1, 1), carpetBorderColor,
                                                zone);
                        }
                    }
                    if (cell.kind != DungeonCellEntrance && !(cell.flags & (1 << DungeonCellFlagStairsUp))) {
                        if (!large && rand() % DungeonSpikeTrapChanceDenominator == 0) {
                            // A spike trap filling the cell. Dungeon layout: cell-relative room, corridor and entrance offsets
                            // in the ten-block grid.
                            Zone::Object trap;
                            trap.position =
                                plasma::Vector<3, Fixed>(x + cx * 10 + 5, y + cy * 10 + 5, bestDepth + cz * 10 + 1);
                            trap.size = plasma::Vector<3, float>(10.0f, 10.0f, 10.0f);
                            trap.rotation = rand() % 4;
                            trap.type = StaticObjectSpikeTrap;
                            trap.transitionTime = rand() % DungeonSpikeTrapPhasePeriod;
                            zone->m_objects.push_back(trap);
                        } else {
                            int objectCount = (int)zone->m_objects.size();
                            if (dungeon.cell(cx, cy, cz + 1).isOpen()) {
                                if (rand() % DungeonCeilingPlantChanceDenominator == 0 &&
                                    (variant == Region::Record::DungeonTemple ||
                                     variant == Region::Record::DungeonCatacombs)) {
                                    // A plant hanging from the ceiling.
                                    Zone::Plant plant;
                                    plant.rotation = rand() / float(RAND_MAX) * 360.0f;
                                    // Ceiling and wall plant layout: cell-relative coordinates and sprite extent scaling.
                                    plant.scale = rand() * 0.04f / float(RAND_MAX) + 0.08f;
                                    // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                                    // The original draws y, x in that order.
                                    plant.position = plasma::Vector<3, Fixed>(
                                        x + cx * 10 + 2 + rand() % 5, y + cy * 10 + 2 + rand() % 5,
                                        (float)(bestDepth + (cz + 1) * 10) - plant.scale * 20.0f);
                                    plant.type = PlantLiana;
                                    zone->m_plants.push_back(plant);
                                }
                                if (variant == Region::Record::DungeonCatacombs ||
                                    variant == Region::Record::DungeonRuins) {
                                    // Vines on the walls.
                                    if (rand() % WallCobwebChanceDenominator == 0 &&
                                        dungeon.cell(cx - 1, cy, cz).isOpen()) {
                                        Zone::Plant vine;
                                        vine.rotation = 0.0f;
                                        vine.scale = rand() * 0.05f / float(RAND_MAX) + 0.08f;
                                        vine.position = plasma::Vector<3, Fixed>(
                                            (float)(x + cx * 10) + vine.scale * 10.0f, y + cy * 10 + 2 + rand() % 5,
                                            (float)(bestDepth + (cz + 1) * 10) - vine.scale * 20.0f);
                                        vine.type = rand() % 2 + PlantCobwebs;
                                        zone->m_plants.push_back(vine);
                                    }
                                    if (rand() % WallCobwebChanceDenominator == 0 &&
                                        dungeon.cell(cx + 1, cy, cz).isOpen()) {
                                        Zone::Plant vine;
                                        vine.rotation = 180.0f;
                                        vine.scale = rand() * 0.05f / float(RAND_MAX) + 0.08f;
                                        vine.position = plasma::Vector<3, Fixed>(
                                            (float)(x + (cx + 1) * 10) - vine.scale * 10.0f,
                                            y + cy * 10 + 2 + rand() % 5,
                                            (float)(bestDepth + (cz + 1) * 10) - vine.scale * 20.0f);
                                        vine.type = rand() % 2 + PlantCobwebs;
                                        zone->m_plants.push_back(vine);
                                    }
                                    if (rand() % WallCobwebChanceDenominator == 0 &&
                                        dungeon.cell(cx, cy + 1, cz).isOpen()) {
                                        Zone::Plant vine;
                                        vine.rotation = 270.0f;
                                        vine.scale = rand() * 0.05f / float(RAND_MAX) + 0.08f;
                                        vine.position = plasma::Vector<3, Fixed>(
                                            x + cx * 10 + 2 + rand() % 5,
                                            (float)(y + (cy + 1) * 10) - vine.scale * 10.0f,
                                            (float)(bestDepth + (cz + 1) * 10) - vine.scale * 20.0f);
                                        vine.type = rand() % 2 + PlantCobwebs;
                                        zone->m_plants.push_back(vine);
                                    }
                                    if (rand() % WallCobwebChanceDenominator == 0 &&
                                        dungeon.cell(cx, cy - 1, cz).isOpen()) {
                                        Zone::Plant vine;
                                        vine.rotation = 90.0f;
                                        vine.scale = rand() * 0.05f / float(RAND_MAX) + 0.08f;
                                        vine.position = plasma::Vector<3, Fixed>(
                                            x + cx * 10 + 2 + rand() % 5, (float)(y + cy * 10) + vine.scale * 10.0f,
                                            (float)(bestDepth + (cz + 1) * 10) - vine.scale * 20.0f);
                                        vine.type = rand() % 2 + PlantCobwebs;
                                        zone->m_plants.push_back(vine);
                                    }
                                }
                            }
                            // Against the open walls: a torch where the wall is solid, and an object, or now and then a
                            // Creature.
                            if (rand() % 3 == 0 && dungeon.cell(cx - 1, cy, cz).isOpen()) {
                                if (rand() % 2) {
                                    plasma::Vector<3, Fixed> position((double)(x + cx * 10) + 0.5,
                                                                      y + cy * 10 + WallFurnishingMinimumLateralOffset +
                                                                          rand() % WallFurnishingLateralRange,
                                                                      bestDepth + cz * 10 + 2);
                                    if (getBlock(position[0] - 1, position[1], position[2], zone).isSolid())
                                        zone->m_plants.push_back(makeTorch(variant, position, 270.0f));
                                }
                                if (rand() % DungeonWallFurnishingChanceDenominator) {
                                    zone->m_objects.push_back(makeWallObject(
                                        plasma::Vector<3, Fixed>((double)(x + cx * 10) + 0.5,
                                                                 y + cy * 10 + WallFurnishingMinimumLateralOffset +
                                                                     rand() % WallFurnishingLateralRange,
                                                                 bestDepth + cz * 10 + 1),
                                        1, zone->cell.style));
                                } else {
                                    Spawn *spawn = new Spawn;
                                    spawn->hostilityType = HostilityAttackableObject;
                                    spawn->race = RaceVase;
                                    spawn->level = level;
                                    spawn->multipliers.hp = 10.0f;
                                    spawn->position =
                                        plasma::Vector<3, Fixed>(x + cx * 10 + 1,
                                                                 y + cy * 10 + WallFurnishingMinimumLateralOffset +
                                                                     rand() % WallFurnishingLateralRange,
                                                                 bestDepth + cz * 10 + 1);
                                    spawn->spawnRadius = 150.0f;
                                    fillInventory(&spawn->inventory, level, dungeonRarity);
                                    zone->m_spawns.push_back(spawn);
                                }
                            }
                            if (rand() % 3 == 0 && dungeon.cell(cx + 1, cy, cz).isOpen()) {
                                if (rand() % 2) {
                                    plasma::Vector<3, Fixed> position((double)(x + (cx + 1) * 10) - 0.5,
                                                                      y + cy * 10 + WallFurnishingMinimumLateralOffset +
                                                                          rand() % 4,
                                                                      bestDepth + cz * 10 + 2);
                                    if (getBlock(position[0] + 1, position[1], position[2], zone).isSolid())
                                        zone->m_plants.push_back(makeTorch(variant, position, 90.0f));
                                }
                                if (rand() % 8) {
                                    zone->m_objects.push_back(makeWallObject(
                                        plasma::Vector<3, Fixed>((double)(x + (cx + 1) * 10) - 0.5,
                                                                 y + cy * 10 + WallFurnishingMinimumLateralOffset +
                                                                     rand() % 4,
                                                                 bestDepth + cz * 10 + 1),
                                        3, zone->cell.style));
                                } else {
                                    Spawn *spawn = new Spawn;
                                    spawn->hostilityType = HostilityAttackableObject;
                                    spawn->race = RaceVase;
                                    spawn->level = level;
                                    spawn->multipliers.hp = 10.0f;
                                    spawn->position = plasma::Vector<3, Fixed>(
                                        x + (cx + 1) * 10 - 1,
                                        y + cy * 10 + WallFurnishingMinimumLateralOffset + rand() % 4,
                                        bestDepth + cz * 10 + 1);
                                    fillInventory(&spawn->inventory, level, dungeonRarity);
                                    spawn->spawnRadius = 150.0f;
                                    zone->m_spawns.push_back(spawn);
                                }
                            }
                            if (rand() % 3 == 0 && dungeon.cell(cx, cy - 1, cz).isOpen()) {
                                if (rand() % 2) {
                                    plasma::Vector<3, Fixed> position(
                                        x + cx * 10 + WallFurnishingMinimumLateralOffset + rand() % 4,
                                        (double)(y + cy * 10) + 0.5, bestDepth + cz * 10 + 2);
                                    if (getBlock(position[0], position[1] - 1, position[2], zone).isSolid())
                                        zone->m_plants.push_back(makeTorch(variant, position, 0.0f));
                                }
                                if (rand() % 8) {
                                    zone->m_objects.push_back(makeWallObject(
                                        plasma::Vector<3, Fixed>(x + cx * 10 + WallFurnishingMinimumLateralOffset +
                                                                     rand() % 4,
                                                                 (double)(y + cy * 10) + 0.5, bestDepth + cz * 10 + 1),
                                        2, zone->cell.style));
                                } else {
                                    Spawn *spawn = new Spawn;
                                    spawn->hostilityType = HostilityAttackableObject;
                                    spawn->race = RaceVase;
                                    spawn->level = level;
                                    spawn->multipliers.hp = 10.0f;
                                    spawn->position = plasma::Vector<3, Fixed>(
                                        x + cx * 10 + WallFurnishingMinimumLateralOffset + rand() % 4, y + cy * 10 + 1,
                                        bestDepth + cz * 10 + 1);
                                    fillInventory(&spawn->inventory, level, dungeonRarity);
                                    spawn->spawnRadius = 150.0f;
                                    zone->m_spawns.push_back(spawn);
                                }
                            }
                            if (rand() % 3 == 0 && dungeon.cell(cx, cy + 1, cz).isOpen()) {
                                if (rand() % 2) {
                                    plasma::Vector<3, Fixed> position(
                                        x + cx * 10 + WallFurnishingMinimumLateralOffset + rand() % 4,
                                        (double)(y + (cy + 1) * 10) - 0.5, bestDepth + cz * 10 + 2);
                                    if (getBlock(position[0], position[1] + 1, position[2], zone).isSolid())
                                        zone->m_plants.push_back(makeTorch(variant, position, 180.0f));
                                } else if (rand() % 8) {
                                    zone->m_objects.push_back(makeWallObject(
                                        plasma::Vector<3, Fixed>(
                                            x + cx * 10 + WallFurnishingMinimumLateralOffset + rand() % 4,
                                            (double)(y + (cy + 1) * 10) - 0.5, bestDepth + cz * 10 + 1),
                                        0, zone->cell.style));
                                } else {
                                    Spawn *spawn = new Spawn;
                                    spawn->hostilityType = HostilityAttackableObject;
                                    spawn->race = RaceVase;
                                    spawn->level = level;
                                    spawn->multipliers.hp = 10.0f;
                                    spawn->position = plasma::Vector<3, Fixed>(
                                        x + cx * 10 + WallFurnishingMinimumLateralOffset + rand() % 4,
                                        y + (cy + 1) * 10 - 1, bestDepth + cz * 10 + 1);
                                    fillInventory(&spawn->inventory, level, dungeonRarity);
                                    spawn->spawnRadius = 150.0f;
                                    zone->m_spawns.push_back(spawn);
                                }
                            }
                            // Stock the objects placed in this cell: chests get one to four items a rarity better than the
                            // dungeon's, shelves and tables items on top.
                            for (int i = objectCount; i < (int)zone->m_objects.size(); i++) {
                                if (zone->m_objects[i].type == StaticObjectChest) {
                                    int count = rand() % 4 + 1;
                                    zone->m_objects[i].inventory.m_inventoryTabs.resize(ChestInventoryTabCount);
                                    for (int k = count; k > 0; k--)
                                        zone->m_objects[i].inventory.add(randomItem(level, dungeonRarity + 1));
                                    zone->m_objects[i].active = StaticObjectStockedChest;
                                } else if (zone->m_objects[i].type == StaticObjectStoneTable ||
                                           zone->m_objects[i].type == StaticObjectTable ||
                                           zone->m_objects[i].type == StaticObjectCastleShelf1 ||
                                           zone->m_objects[i].type == StaticObjectCastleShelf2 ||
                                           zone->m_objects[i].type == StaticObjectCastleShelf3) {
                                    for (int ix = 0; ix < zone->m_objects[i].size[0]; ix++) {
                                        for (int iy = 0; iy < zone->m_objects[i].size[1]; iy++) {
                                            if (rand() % 10 != 0)
                                                continue;
                                            Zone::ZoneItem item;
                                            item.flags = (1 << ZoneItemFlagFixed);
                                            item.scale = 0.06f;
                                            if (rand() % ShelfRareLootChanceDenominator) {
                                                switch (rand() % ShelfCommonLootChoiceCount) {
                                                case 0:
                                                    item.item.category = ItemCategoryMaterial;
                                                    item.item.id = MaterialItemWaterFlask;
                                                    break;
                                                case 1:
                                                    item.item.category = ItemCategoryCandle;
                                                    item.item.id = shelfItemId;
                                                    item.item.modifier = rand() % CandleModelVariants;
                                                    break;
                                                case 2:
                                                    item.item.category = ItemCategoryMaterial;
                                                    item.item.id = MaterialItemYarn;
                                                    item.item.material =
                                                        rand() % (ItemMaterialCotton - ItemMaterialSilk + 1) +
                                                        ItemMaterialSilk;
                                                    break;
                                                case 3:
                                                    item.item.category = ItemCategoryConsumable;
                                                    item.item.id = ConsumableBomb;
                                                    break;
                                                }
                                            } else {
                                                switch (rand() % ShelfIngredientChoiceCount) {
                                                case 0:
                                                    item.item.category = ItemCategoryConsumable;
                                                    item.item.id = ConsumableLifePotion;
                                                    break;
                                                case 1:
                                                    item.item.category = ItemCategoryConsumable;
                                                    item.item.id = ConsumableGinsengSoup;
                                                    break;
                                                case 2:
                                                    item.item.category = ItemCategoryConsumable;
                                                    item.item.id = ConsumableSnowBerryMash;
                                                    break;
                                                case 3: {
                                                    int coins =
                                                        (int)pow(2.0f, (rand() * 2.0f / float(RAND_MAX) + 1.0f) *
                                                                           (level * 0.25f));
                                                    item.item.category = ItemCategoryCoin;
                                                    item.item.material = ItemMaterialCopper;
                                                    item.item.level = (short)(rand() % (coins * 2 + 2));
                                                    break;
                                                }
                                                case 4:
                                                    item.item = randomItem(level, dungeonRarity);
                                                    break;
                                                case 5:
                                                    item.item = randomFormula(level, dungeonRarity);
                                                    break;
                                                }
                                            }
                                            item.position = zone->m_objects[i].position +
                                                            plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                                (float)ix - zone->m_objects[i].size[0] * 0.5f + 0.5f,
                                                                (float)iy - zone->m_objects[i].size[1] * 0.5f + 0.5f,
                                                                zone->m_objects[i].size[2]));
                                            item.rotation = rand() * 360.0f / float(RAND_MAX);
                                            zone->m_items.push_back(item);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (cell.kind != DungeonCellEntrance) {
                    if (dungeon.cell(cx, cy, cz + 1).kind != DungeonCellCorridor) {
                        // Under a closed ceiling, arches from the stair sprite towards each side that is not a corridor.
                        int bx = x + cx * 10;
                        int by = y + cy * 10;
                        if (stairs && getBlock(bx, by, bestDepth + (cz + 1) * 10, zone).isSolid()) {
                            if (dungeon.cell(cx - 1, cy, cz).kind != DungeonCellCorridor) {
                                plasma::Vector<4, int> placementMargins(
                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                                placeSprite(stairs, plasma::Vector<3, int>(bx, by, bestDepth + cz * 10), 0, 70,
                                            PlacementPlain, zone, false, placementMargins, 0);
                            }
                            if (dungeon.cell(cx + 1, cy, cz).kind != DungeonCellCorridor) {
                                plasma::Vector<4, int> placementMargins(
                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                                placeSprite(stairs, plasma::Vector<3, int>(bx, by, bestDepth + cz * 10), 2, 70,
                                            PlacementPlain, zone, false, placementMargins, 0);
                            }
                            if (dungeon.cell(cx, cy - 1, cz).kind != DungeonCellCorridor) {
                                // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                                plasma::Vector<4, int> placementMargins(
                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                                placeSprite(stairs, plasma::Vector<3, int>(bx, by, bestDepth + cz * 10), 3, 70,
                                            PlacementPlain, zone, false, placementMargins, 0);
                            }
                            if (dungeon.cell(cx, cy + 1, cz).kind != DungeonCellCorridor) {
                                plasma::Vector<4, int> placementMargins(
                                    plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                                placeSprite(stairs, plasma::Vector<3, int>(bx, by, bestDepth + cz * 10), 1, 70,
                                            PlacementPlain, zone, false, placementMargins, 0);
                            }
                        }
                        if (variant == Region::Record::DungeonPalace && rand() % 10 == 0) {
                            // A chandelier under a palace cell's ceiling.
                            Zone::Plant plant;
                            // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                            plant.type = PlantChandelier;
                            plant.position = plasma::Vector<3, Fixed>(x + cx * 10 + 5, y + cy * 10 + 5,
                                                                      (double)(bestDepth + (cz + 1) * 10) - 2.7) +
                                             plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                            plant.scale = 0.1f;
                            plant.rotation = 0.0f;
                            plant.flags = (1 << PlantFlagEmissive);
                            plant.color = plantColor;
                            zone->m_plants.push_back(plant);
                        }
                    }
                }
                if (cell.flags & (1 << DungeonCellFlagGuard)) {
                    // An elite guard of the dungeon's races in the cell.
                    Spawn *spawn = new Spawn;
                    // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                    spawn->position = plasma::Vector<3, Fixed>((float)(x + cx * 10) + 4.5f, (float)(y + cy * 10) + 4.5f,
                                                               bestDepth + cz * 10 + 1);
                    spawn->hostilityType = HostilityEnemy;
                    spawn->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                    spawn->level = level;
                    spawn->rarity = (unsigned char)dungeonRarity;
                    if (spawn->level < 1)
                        spawn->level = 1;
                    spawn->race = races[rand() % races.size()];
                    spawn->inventory.add(randomItem(spawn->level, randomRarity(spawn->rarity, true)));
                    spawn->appearance.appearanceFlags |= (1 << AppearanceFlagTinted);
                    spawn->m_behavior = new CombatBehavior(20.0f);
                    Zone::GenerationMarker record;
                    record.kind = GenerationMarkerGuard;
                    record.position = spawn->position;
                    record.race = spawn->race;
                    record.spawnIndex = (int)zone->m_spawns.size();
                    zone->m_generationMarkers.push_back(record);
                    int count = rand() % GuardPotionCountRange;
                    Item potion;
                    potion.category = ItemCategoryConsumable;
                    potion.id = ConsumableLifePotion;
                    potion.level = (short)spawn->level;
                    for (int k = count; k > 0; k--)
                        spawn->inventory.add(potion);
                    spawn->spawnRadius = 150.0f;
                    spawn->missionResident = true;
                    zone->m_spawns.push_back(spawn);
                }
            }
        }
    }
    if (variant == Region::Record::DungeonTemple) {
        // The temple's top: a hall over the centre cell and a spiral stair down a shaft to the entrance
        // level. Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        plasma::Vector<2, int> center(width / 2 - 1, dungeon.sizeY() / 2 - 1);
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        int bottom = bestDepth + entranceCell[2] * 10 + 8;
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        int top = bestDepth + (entranceCell[2] + width / 2) * 10 - 66;
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        fillBox(x + center[0] * 10 - 2, y + center[1] * 10 - 2, top, plasma::Vector<3, int>(14, 14, 16), wallColor,
                roughness, zone, 0);
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        cutBox(x + center[0] * 10, y + center[1] * 10 - 2, top, plasma::Vector<3, int>(10, 14, 10), zone, false);
        cutBox(x + center[0] * 10 - 2, y + center[1] * 10, top, plasma::Vector<3, int>(14, 10, 10), zone, false);
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        cutBox(x + center[0] * 10, y + center[1] * 10, bottom, plasma::Vector<3, int>(10, 10, top - bottom), zone,
               false);
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        int height = 30 - bottom + top;
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        fillBox(x + center[0] * 10 + 4, y + center[1] * 10 + 4, bottom - 30, plasma::Vector<3, int>(2, 2, height),
                wallColor, roughness, zone, 0);
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        for (int z = bottom - 29; 30 - bottom + z < height; z++) {
            switch ((30 - bottom + z) & SpiralStairPhaseMask) {
                // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
                // Geometry.
            case 0:
                fillBox(x + center[0] * 10, y + center[1] * 10, z, plasma::Vector<3, int>(5, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
                // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
                // Geometry.
            case 1:
                fillBox(x + center[0] * 10 + 5, y + center[1] * 10, z, plasma::Vector<3, int>(1, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
                // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
                // Geometry.
            case 2:
                fillBox(x + center[0] * 10 + 6, y + center[1] * 10, z, plasma::Vector<3, int>(5, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
                // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
                // Geometry.
            case 3:
                fillBox(x + center[0] * 10 + 6, y + center[1] * 10 + 5, z, plasma::Vector<3, int>(5, 1, 1), wallColor,
                        roughness, zone, 0);
                break;
                // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
                // Geometry.
            case 4:
                fillBox(x + center[0] * 10 + 6, y + center[1] * 10 + 6, z, plasma::Vector<3, int>(5, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
                // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
                // Geometry.
            case 5:
                fillBox(x + center[0] * 10 + 5, y + center[1] * 10 + 6, z, plasma::Vector<3, int>(1, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
                // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
                // Geometry.
            case 6:
                fillBox(x + center[0] * 10, y + center[1] * 10 + 6, z, plasma::Vector<3, int>(5, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
                // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
                // Geometry.
            case 7:
                fillBox(x + center[0] * 10, y + center[1] * 10 + 5, z, plasma::Vector<3, int>(5, 1, 1), wallColor,
                        roughness, zone, 0);
                break;
            }
        }
    } else if (variant == Region::Record::DungeonPyramid) {
        // The pyramid's top: a chamber with two long cross passages and the same spiral stair.
        plasma::Vector<2, int> center(width / 2 - 1, dungeon.sizeY() / 2 - 1);
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        int bottom = bestDepth + entranceCell[2] * 10 + 8;
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        int top = bestDepth + (entranceCell[2] + width / 2) * 10 - 72;
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        fillBox(x + center[0] * 10 - 2, y + center[1] * 10 - 2, top, plasma::Vector<3, int>(14, 14, 14), wallColor,
                roughness, zone, 0);
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        top -= 12;
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        cutBox(x + center[0] * 10 + 2, y + center[1] * 10 - 32, top, plasma::Vector<3, int>(6, 74, 6), zone, false);
        // Dungeon layout: cell-relative spiral-stair chamber, center pillar and eight-sector step
        // Geometry.
        cutBox(x + center[0] * 10 - 32, y + center[1] * 10 + 2, top, plasma::Vector<3, int>(74, 6, 6), zone, false);
        cutBox(x + center[0] * 10, y + center[1] * 10, bottom, plasma::Vector<3, int>(10, 10, top - bottom + 6), zone,
               false);
        int height = 30 - bottom + top;
        fillBox(x + center[0] * 10 + 4, y + center[1] * 10 + 4, bottom - 30, plasma::Vector<3, int>(2, 2, height),
                wallColor, roughness, zone, 0);
        for (int z = bottom - 29; 30 - bottom + z < height; z++) {
            switch ((30 - bottom + z) & 7) {
            case 0:
                fillBox(x + center[0] * 10, y + center[1] * 10, z, plasma::Vector<3, int>(5, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
            case 1:
                fillBox(x + center[0] * 10 + 5, y + center[1] * 10, z, plasma::Vector<3, int>(1, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
            case 2:
                fillBox(x + center[0] * 10 + 6, y + center[1] * 10, z, plasma::Vector<3, int>(5, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
            case 3:
                fillBox(x + center[0] * 10 + 6, y + center[1] * 10 + 5, z, plasma::Vector<3, int>(5, 1, 1), wallColor,
                        roughness, zone, 0);
                break;
            case 4:
                fillBox(x + center[0] * 10 + 6, y + center[1] * 10 + 6, z, plasma::Vector<3, int>(5, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
            case 5:
                fillBox(x + center[0] * 10 + 5, y + center[1] * 10 + 6, z, plasma::Vector<3, int>(1, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
            case 6:
                fillBox(x + center[0] * 10, y + center[1] * 10 + 6, z, plasma::Vector<3, int>(5, 5, 1), wallColor,
                        roughness, zone, 0);
                break;
            case 7:
                fillBox(x + center[0] * 10, y + center[1] * 10 + 5, z, plasma::Vector<3, int>(5, 1, 1), wallColor,
                        roughness, zone, 0);
                break;
            }
        }
    } else if (variant == Region::Record::DungeonRuins || variant == Region::Record::DungeonCatacombs) {
        // Over a cave the ground sinks in hollows: dry interior blocks near the surface become air.
        int cells = dungeon.sizeY();
        for (int cx = 0, bx0 = x; cx < width; cx++, bx0 += 10) {
            for (int cy = 0, by0 = y; cy < cells; cy++, by0 += 10) {
                for (int bx = bx0; bx < bx0 + 10; bx++) {
                    for (int by = by0; by < by0 + 10; by++) {
                        // Ruins/catacomb ground hollow noise coefficient: float hollow = noise(bx * 0.1, by * 0.1) * 2.0f.
                        Field *field = getField(bx, by, zone);
                        // Ruins/catacomb ground hollow noise coefficient: hollow += noise(bx * 0.05, by * 0.05) * 8.0f.
                        float hollow = noise(bx * 0.1, by * 0.1) * 2.0f;
                        hollow += noise(bx * 0.05, by * 0.05) * 8.0f;
                        if (hollow > 0.0f)
                            hollow = 0.0f;
                        int bottom = (int)((float)(field->getBaseZ() + field->size()) + hollow);
                        for (int z = field->getBaseZ() + field->size(); z >= bottom; z--) {
                            if ((getBlock(bx, by, z, zone).type & (1 << BlockFlagInterior)) &&
                                getBlock(bx, by, z, zone).isAboveWater())
                                setBlock(bx, by, z, airCube, zone);
                        }
                    }
                }
            }
        }
    }

    // Creatures for each room: a guard patrolling its corners with followers, and groups of the
    // dungeon's races.
    for (int rx = 0; rx < 3; rx++) {
        for (int ry = 0; ry < 3; ry++) {
            for (int rz = 0; rz < 3; rz++) {
                Dungeon::Room &room = rooms[rx][ry][rz];
                if (!room.used || room.noPatrol)
                    continue;
                plasma::Vector<3, int> size = plasma::Vector<3, int>(3, 3, 1) + room.size;
                // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                plasma::Vector<3, int> corners[4];
                corners[0] = plasma::Vector<3, int>(rx * 7 + 2 + room.offset[0], ry * 7 + 2 + room.offset[1],
                                                    rz * 7 + 3 + room.offset[2]);
                corners[1] = plasma::Vector<3, int>(corners[0][0] + size[0] - 1, corners[0][1], corners[0][2]);
                corners[2] =
                    plasma::Vector<3, int>(corners[0][0] + size[0] - 1, corners[0][1] + size[1] - 1, corners[0][2]);
                corners[3] = plasma::Vector<3, int>(corners[0][0], corners[0][1] + size[1] - 1, corners[0][2]);
                for (int i = 0; i < 4; i++)
                    dungeon.inverseTransform(corners[i][0], corners[i][1], corners[i][2]);
                if (size[0] * size[1] > PatrolLeaderMinimumRoomArea && !styles.empty()) {
                    Dungeon::Style &style = styles[rand() % styles.size()];
                    if (!style.guardRaces.empty()) {
                        int start = rand() % 4;
                        Spawn *leader = new Spawn;
                        // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                        plasma::Vector<3, int> &corner = corners[start % 4];
                        leader->position = plasma::Vector<3, Fixed>(x + corner[0] * 10 + 5, y + corner[1] * 10 + 5,
                                                                    bestDepth + corner[2] * 10 + 1);
                        leader->level = level;
                        if (large) {
                            leader->hostilityType = HostilityFriendlyNpc;
                            leader->race = RaceElfMale + rand() % 2;
                        } else {
                            leader->hostilityType = HostilityEnemy;
                            leader->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                            leader->race = style.guardRaces[rand() % style.guardRaces.size()];
                            leader->rarity = (unsigned char)dungeonRarity;
                        }
                        SequentialBehavior *sequence = new SequentialBehavior;
                        sequence->m_behaviors.push_back(new CombatBehavior(20.0f));
                        WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
                        sequence->m_behaviors.push_back(walk);
                        switch (rand() % 3) {
                        case 0:
                            for (int k = 0; k < 4; k++, start++) {
                                plasma::Vector<3, int> &point = corners[start % 4];
                                // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                                walk->m_path.push_back(plasma::Vector<3, Fixed>(
                                    x + point[0] * 10 + 5, y + point[1] * 10 + 5, bestDepth + point[2] * 10 + 1));
                            }
                            break;
                        case 1:
                            for (int k = 0; k < 4; k++, start++) {
                                plasma::Vector<3, int> &point = corners[3 - start % 4];
                                walk->m_path.push_back(plasma::Vector<3, Fixed>(
                                    x + point[0] * 10 + 5, y + point[1] * 10 + 5, bestDepth + point[2] * 10 + 1));
                            }
                            break;
                        case 2: {
                            // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                            walk->m_path.push_back(plasma::Vector<3, Fixed>(
                                x + corner[0] * 10 + 5, y + corner[1] * 10 + 5, bestDepth + corner[2] * 10 + 1));
                            plasma::Vector<3, int> &next = corners[(start + 1) % 4];
                            // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                            walk->m_path.push_back(plasma::Vector<3, Fixed>(x + next[0] * 10 + 5, y + next[1] * 10 + 5,
                                                                            bestDepth + next[2] * 10 + 1));
                            plasma::Vector<3, int> &opposite = corners[(start + 2) % 4];
                            // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                            walk->m_path.push_back(plasma::Vector<3, Fixed>(
                                x + opposite[0] * 10 + 5, y + opposite[1] * 10 + 5, bestDepth + opposite[2] * 10 + 1));
                            walk->m_path.push_back(plasma::Vector<3, Fixed>(x + next[0] * 10 + 5, y + next[1] * 10 + 5,
                                                                            bestDepth + next[2] * 10 + 1));
                            break;
                        }
                        }
                        leader->m_behavior = sequence;
                        leader->spawnRadius = 150.0f;
                        int potions = rand() % 2;
                        Item potion;
                        potion.level = (short)leader->level;
                        potion.category = ItemCategoryConsumable;
                        potion.id = ConsumableLifePotion;
                        for (int k = potions; k > 0; k--)
                            leader->inventory.add(potion);
                        leader->creatureId =
                            generationKey(zone->m_position[0], zone->m_position[1], (int)zone->m_spawns.size());
                        zone->m_spawns.push_back(leader);
                        if (!large && !style.followerRaces.empty()) {
                            // Followers of other races keep to the guard.
                            for (int k = rand() % PatrolFollowerCountRange + 1; k > 0; k--) {
                                Spawn *follower = new Spawn;
                                follower->position = leader->position;
                                follower->level = leader->level;
                                follower->hostilityType = HostilityEnemy;
                                follower->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                                follower->race = style.followerRaces[rand() % style.followerRaces.size()];
                                follower->rarity = leader->rarity;
                                SequentialBehavior *behaviors = new SequentialBehavior;
                                behaviors->m_behaviors.push_back(new CombatBehavior(20.0f));
                                CompanionBehavior *companion = new CompanionBehavior;
                                companion->m_ownerId = leader->creatureId;
                                behaviors->m_behaviors.push_back(companion);
                                WalkPathBehavior *home = new WalkPathBehavior(2.0f);
                                home->m_path.push_back(leader->position);
                                behaviors->m_behaviors.push_back(home);
                                follower->m_behavior = behaviors;
                                follower->multipliers.damage *= 0.5f;
                                follower->spawnRadius = 150.0f;
                                zone->m_spawns.push_back(follower);
                            }
                        }
                    }
                }
                if (!races.empty() && !room.guard) {
                    // Groups of two or three standing in a circle at successive corners.
                    int groups = rand() % 2 + 1;
                    int start = rand() % 4;
                    if (size[0] * size[1] > 7)
                        groups++;
                    for (int g = groups; g > 0; g--, start++) {
                        int count = rand() % 2 + 2;
                        plasma::Vector<3, int> &corner = corners[start % 4];
                        for (int i = 0; i < count; i++) {
                            float angle = (float)(i * 2 * 3.141592653589793 / count);
                            // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                            Spawn *member = new Spawn;
                            // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                            member->position = plasma::Vector<3, Fixed>(
                                (float)(x + corner[0] * 10 + 5) + cos(angle) * 2.0f,
                                (float)(y + corner[1] * 10 + 5) + sin(angle) * 2.0f, bestDepth + corner[2] * 10 + 1);
                            member->rotation = (float)(angle / 3.141592653589793 * 180.0 + 90.0);
                            member->level = level;
                            if (large) {
                                member->hostilityType = HostilityFriendlyNpc;
                                member->race = RaceElfMale + rand() % 2;
                            } else {
                                member->hostilityType = HostilityEnemy;
                                member->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                                member->race = races[rand() % races.size()];
                                member->rarity = (unsigned char)dungeonRarity;
                            }
                            SequentialBehavior *behaviors = new SequentialBehavior;
                            behaviors->m_behaviors.push_back(new CombatBehavior(20.0f));
                            WalkPathBehavior *home = new WalkPathBehavior(2.0f);
                            home->m_path.push_back(member->position);
                            behaviors->m_behaviors.push_back(home);
                            member->m_behavior = behaviors;
                            // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                            member->spawnRadius = 150.0f;
                            member->multipliers.damage *= 0.75f;
                            int potions = rand() % 2;
                            Item potion;
                            potion.level = (short)member->level;
                            potion.category = ItemCategoryConsumable;
                            potion.id = ConsumableLifePotion;
                            for (int k = potions; k > 0; k--)
                                member->inventory.add(potion);
                            zone->m_spawns.push_back(member);
                        }
                    }
                    // Another circle in the middle of a large room.
                    if (size[0] > 2 && size[1] > 2 && rand() % 2) {
                        plasma::Vector<3, int> middle = (corners[0] + corners[2]) / 2;
                        if (dungeon.cell(middle[0], middle[1], middle[2]).kind == DungeonCellCorridor) {
                            int count = rand() % 2 + 2;
                            for (int i = 0; i < count; i++) {
                                float angle = (float)(i * 2 * 3.141592653589793 / count);
                                // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                                Spawn *member = new Spawn;
                                // Dungeon layout: cell-relative room, corridor and entrance offsets in the ten-block grid.
                                member->position =
                                    plasma::Vector<3, Fixed>((float)(x + middle[0] * 10 + 5) + cos(angle) * 2.0f,
                                                             (float)(y + middle[1] * 10 + 5) + sin(angle) * 2.0f,
                                                             bestDepth + middle[2] * 10 + 1);
                                member->rotation = (float)(angle / 3.141592653589793 * 180.0 + 90.0);
                                member->level = level;
                                if (large) {
                                    member->hostilityType = HostilityFriendlyNpc;
                                    member->race = RaceElfMale + rand() % 2;
                                } else {
                                    member->hostilityType = HostilityEnemy;
                                    member->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                                    member->race = races[rand() % races.size()];
                                    member->rarity = (unsigned char)dungeonRarity;
                                }
                                member->multipliers.damage *= 0.75f;
                                SequentialBehavior *behaviors = new SequentialBehavior;
                                behaviors->m_behaviors.push_back(new CombatBehavior(20.0f));
                                WalkPathBehavior *home = new WalkPathBehavior(2.0f);
                                home->m_path.push_back(member->position);
                                behaviors->m_behaviors.push_back(home);
                                member->m_behavior = behaviors;
                                member->spawnRadius = 150.0f;
                                int potions = rand() % 2;
                                Item potion;
                                potion.level = (short)member->level;
                                potion.category = ItemCategoryConsumable;
                                potion.id = ConsumableLifePotion;
                                for (int k = potions; k > 0; k--)
                                    member->inventory.add(potion);
                                zone->m_spawns.push_back(member);
                            }
                        }
                    }
                }
            }
        }
    }
}

namespace {
enum {
    PopulationGroundEscapeStepLimit = 100
};
}

// Populates a zone afresh with a band of humanoids: not the spawn point's zone, not while a
// creature made from one of its spawns lives, not within 100 blocks of a player, and only where
// the landscape has a record. The zone's spawns past its first m_baseSpawnCount are replaced: a
// leader of a random humanoid race at the zone's centre, walking a path of 20 random points, and
// up to three followers (rarely more than one) that keep with it. The level is drawn from the
// players' level range; days is added to the zone's m_populationDays.
void World::populateZone(Zone *zone, int days)
{
    Region::Cell *cell = getCell(zone->m_position[0], zone->m_position[1]);
    if (!cell || cell->kind)
        return;
    plasma::Vector<2, int> spawnZone((int)m_spawnPoint[0] / 256, (int)m_spawnPoint[1] / 256);
    if (zone->m_position == spawnZone)
        return;
    for (int i = zone->m_baseSpawnCount; i < (int)zone->m_spawns.size(); i++) {
        std::map<long long, Creature *>::iterator it = m_creatures.find(zone->m_spawns[i]->creatureId);
        if (it != m_creatures.end() && it->second)
            return;
    }

    plasma::Vector<3, Fixed> centre(zone->m_position[0] * 256 + (BlocksPerZone / 2),
                                    zone->m_position[1] * 256 + (BlocksPerZone / 2), 0);
    int centreX = (int)centre[0];
    int centreY = (int)centre[1];
    Field *field = getField(centreX, centreY, zone);
    centre[2] = field->m_baseZ + field->m_size;
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        if (it->second && it->second->m_entityData.hostilityType == HostilityPlayer) {
            plasma::Vector<3, float> offset(it->second->m_entityData.position - centre);
            if (offset.squaredLength() < 10000.0f)
                return;
        }
    }
    if (!nearestRegionRecord(centreX, centreY))
        return;

    int lowestLevel = 0;
    int highestLevel = 0;
    std::list<Creature *> players;
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        if (it->second && it->second->m_entityData.hostilityType == HostilityPlayer) {
            players.push_back(it->second);
            if (lowestLevel == 0 || it->second->m_entityData.level < lowestLevel)
                lowestLevel = it->second->m_entityData.level;
            if (highestLevel == 0 || it->second->m_entityData.level > highestLevel)
                highestLevel = it->second->m_entityData.level;
        }
    }
    int level = rand() % (highestLevel - lowestLevel + 1) + lowestLevel;

    zone->m_populationDays += days;
    for (int i = zone->m_baseSpawnCount; i < (int)zone->m_spawns.size(); i++)
        delete zone->m_spawns[i];
    zone->m_spawns.resize(zone->m_baseSpawnCount);

    std::vector<int> races;
    races.push_back(RaceElfMale);
    races.push_back(RaceDwarfMale);
    races.push_back(RaceOrcMale);
    races.push_back(RaceGoblinMale);
    races.push_back(RaceLizardmanMale);
    races.push_back(RaceUndeadMale);
    races.push_back(RaceHumanMale);
    races.push_back(RaceFrogmanMale);
    races.push_back(RaceGnobold);
    races.push_back(RaceGnoll);
    races.push_back(RaceSpikeCreature);
    races.push_back(RaceWitch);
    races.push_back(RaceWizard);
    int race = races[rand() % races.size()];

    Spawn *leader = new Spawn;
    leader->hostilityType = rand() % 2 ? HostilityEnemy : HostilityFriendlyNpc;
    leader->position = centre;
    leader->race =
        randomRaceVariant((int)leader->position[0], (int)leader->position[1], (int)leader->position[2], race);
    leader->level = level;
    leader->rarity = ItemRarityWhite;
    SequentialBehavior *sequence = new SequentialBehavior;
    sequence->m_behaviors.push_back(new CombatBehavior(20.0f));
    sequence->m_behaviors.push_back(new LookAtPlayerBehavior);
    WalkPathBehavior *walk = new WalkPathBehavior(10.0f);
    // Twenty random points of the zone (16 blocks from its edges), each raised from the centre
    // column's m_groundZ out of the ground.
    for (int i = 20; i > 0; i--) {
        plasma::Vector<3, Fixed> point;
        point[1] = zone->m_position[1] * 256 + 16 + rand() % 224;
        point[0] = zone->m_position[0] * 256 + 16 + rand() % 224;
        point[2] = getField(centreX, centreY, zone)->m_groundZ;
        for (int step = 0; step < PopulationGroundEscapeStepLimit; step++) {
            int type = getBlock(point[0], point[1], point[2], zone).type & ~BlockFlagMask;
            if (type == BlockAir || type == BlockWater)
                break;
            point[2] = point[2] + 1;
        }
        walk->m_path.push_back(point);
    }
    sequence->m_behaviors.push_back(walk);
    sequence->m_behaviors.push_back(new RandomWalkBehavior);
    leader->m_behavior = sequence;
    leader->creatureId =
        ((long long)zone->m_position[1] * 65536 + zone->m_position[0]) * 256 + (int)zone->m_spawns.size();
    int potions = rand() % 2;
    Item potion;
    potion.level = (short)leader->level;
    potion.category = ItemCategoryConsumable;
    potion.id = ConsumableLifePotion;
    for (int i = potions; i > 0; i--)
        leader->inventory.add(potion);
    zone->m_spawns.push_back(leader);

    int followers = (int)(pow(rand() / float(RAND_MAX), 2.0f) * 4.0f);
    for (int i = 1; i <= followers; i++) {
        Spawn *follower = new Spawn;
        follower->hostilityType = leader->hostilityType;
        follower->position = leader->position;
        // The leader's position, not the follower's, moves on by one block in x every other follower and
        // in y every second one.
        leader->position[0] += Fixed(i % 2);
        leader->position[1] += Fixed(i / 2);
        follower->race =
            randomRaceVariant((int)follower->position[0], (int)follower->position[1], (int)follower->position[2], race);
        follower->level = leader->level;
        follower->rarity = leader->rarity;
        SequentialBehavior *followerSequence = new SequentialBehavior;
        followerSequence->m_behaviors.push_back(new CombatBehavior(20.0f));
        followerSequence->m_behaviors.push_back(new LookAtPlayerBehavior);
        followerSequence->m_behaviors.push_back(new CompanionBehavior(leader->creatureId));
        followerSequence->m_behaviors.push_back(new WalkPathBehavior(*walk));
        followerSequence->m_behaviors.push_back(new RandomWalkBehavior);
        follower->m_behavior = followerSequence;
        for (int k = rand() % 2; k > 0; k--)
            follower->inventory.add(potion);
        zone->m_spawns.push_back(follower);
    }

    for (int i = zone->m_baseSpawnCount; i < (int)zone->m_spawns.size(); i++) {
        zone->m_spawns[i]->creatureId =
            ((long long)zone->m_position[1] * GeneratedCreatureZoneRadix + zone->m_position[0]) *
                GeneratedCreatureIndexRadix +
            i;
        fillAppearance(zone->m_spawns[i]->race, zone->m_spawns[i]->appearance, 0);
        equip(zone->m_spawns[i], false);
    }
}

namespace {
enum {
    FirelandsRegionChanceDenominator = 10
};
enum {
    RegionHeightRandomRange = 50
};
enum {
    RegionHeightMinimum = 20
};
enum {
    OceanHeightSubtraction = 100
};
enum {
    OceanDepthLimit = 100
};
enum {
    LandscapeCenterMargin = 512
};
enum {
    LandscapeCenterRandomRange = 15360
};
}

// Chooses the region's landscape parameters and a centre on the 2048-block grid. The spawn region
// keeps the world's spawn column and a positive count.
World::RegionRecord *World::generateRegionRecord(int x, int y)
{
    if (x < 0 || y < 0 || x >= 1024 || y >= 1024)
        return 0;
    if (m_regionRecords[x][y])
        // Region RNG seed mixes the region coordinates with multiplier 3 and salt 4234.
        return m_regionRecords[x][y];
    srand(y * 1024 + m_generationSeed * 3 + x + 4234);
    RegionRecord *record = new RegionRecord;
    record->seed = y * 1024 + x + m_generationSeed;
    record->firelands = false;
    bool atSpawn = x == (int)m_spawnPoint[0] / BlocksPerZone / ZonesPerRegion &&
                   y == (int)m_spawnPoint[1] / BlocksPerZone / ZonesPerRegion;
    if (rand() % 2 && !atSpawn) {
        // Climate distribution: independent humidity/temperature ranges for cold, temperate and firelands
        // regions.
        if (rand() % 2)
            record->humidity = rand() * 0.1f / float(RAND_MAX) + 0.9f;
        else
            record->humidity = rand() * 0.1f / float(RAND_MAX);
        if (rand() % 2)
            record->temperature = rand() * 0.1f / float(RAND_MAX) + 0.9f;
        else
            record->temperature = rand() * 0.1f / float(RAND_MAX);
    } else {
        record->humidity = rand() * 0.4f / float(RAND_MAX) + 0.3f;
        record->temperature = (float)(rand() * 0.4 / double(RAND_MAX) + 0.4f);
        if (!atSpawn && rand() % FirelandsRegionChanceDenominator == 0) {
            record->firelands = true;
            record->temperature = rand() * 0.2f / float(RAND_MAX) + 0.8f;
            record->humidity = rand() * 0.5f / float(RAND_MAX);
        }
    }
    if (atSpawn)
        record->column = plasma::Vector<2, int>((int)m_spawnPoint[0], (int)m_spawnPoint[1]);
    else {
        int columnY = y * BlocksPerRegion + rand() % LandscapeCenterRandomRange + LandscapeCenterMargin;
        int columnX = x * BlocksPerRegion + LandscapeCenterMargin + rand() % LandscapeCenterRandomRange;
        record->column = plasma::Vector<2, int>(columnX, columnY);
    }
    record->column[0] = record->column[0] / 2048 * 2048 + 1024;
    // Noise field coefficients: coordinate frequency, independent channel offsets and output
    // amplitude.
    record->column[1] = record->column[1] / 2048 * 2048 + 1024;
    // Noise field coefficients: coordinate frequency, independent channel offsets and output
    // amplitude.
    float baseHeight = (noise(x * 1.4 + m_generationSeed, y * 1.4 + m_generationSeed + 843.0) + 1.0f) * 100.0f - 70.0f;
    baseHeight += noise(x * 4.0 + m_generationSeed, y * 4.0 + m_generationSeed + 843.0) * 30.0f;
    record->height = (int)baseHeight;
    if (record->height <= 0) {
        record->firelands = false;
        if (atSpawn) {
            record->height = rand() % RegionHeightRandomRange + RegionHeightMinimum;
        } else {
            record->height -= OceanHeightSubtraction;
            if (record->height < -100)
                record->height = -OceanDepthLimit;
            if (rand() % 2) {
                record->humidity = rand() * 0.4f / float(RAND_MAX) + 0.3f;
                record->temperature = (float)(rand() * 0.4 / double(RAND_MAX) + 0.4f);
            } else {
                record->humidity = rand() * 0.1f / float(RAND_MAX) + 0.9f;
                record->temperature = rand() * 0.1f / float(RAND_MAX) + 0.9f;
            }
        }
    }
    m_regionRecords[x][y] = record;
    return record;
}

// A scored position in a region's 8 x 8 record grid or its 64 x 64 zone grid. Both sort instances
// compare the score alone.
struct ScoredRegionCell {
    float score;
    plasma::Vector<2, int> cell;
    ScoredRegionCell(float score, const plasma::Vector<2, int> &cell)
        : score(score),
          cell(cell)
    {}
};

namespace {
enum {
    HangingVineChanceDenominator = 20
};
enum {
    HangingVineLengthRange = 10
};
}

// A blob of foliage around (x, y, z): a flattened ellipsoid of the given radius and height whose
// top is bumped by noise, shaded from color at the bottom to lightColor at the top, of
// destructible type 8 blocks; interior blocks are kept. Foliage of kind 4 trees now and then drops
// a hanging vine below a block.
void World::growFoliage(int x, int y, int z, float radius, float height, plasma::Vector<3, float> color,
                        plasma::Vector<3, float> lightColor, int kind, Zone *zone)
{
    double centerX = x + 0.5;
    double centerY = y + 0.5;
    double centerZ = z + 0.5;
    int seedX = rand();
    int seedY = rand();
    for (int i = (int)(centerX - radius - 1.0); i <= centerX + radius + 1.0; i++) {
        // Foliage crown outline noise scales/amplitudes.
        for (int j = (int)(centerY - radius - 1.0); j <= centerY + radius + 1.0; j++) {
            float bump = (float)(noise(i * 0.04 + seedX, j * 0.04 + seedY) * 0.5 +
                                 noise(i * 0.1 + seedX, j * 0.1 + seedY) * 0.3f);
            double across = ((j - centerY) / radius) * ((j - centerY) / radius) +
                            ((i - centerX) / radius) * ((i - centerX) / radius);
            // Tree shape profile: Foliage vertical aspect ratio.
            for (int k = (int)(centerZ + height + 1.0); k >= centerZ - height - 1.0; k--) {
                double up = (k - centerZ) / height * 1.8f + bump;
                if (up * up + across > 1.0)
                    continue;
                if (getBlock(i, j, k, zone).type & (1 << BlockFlagInterior))
                    continue;
                float shade = (float)((up + 1.0) * 0.5);
                if (shade < 0.0f)
                    shade = 0.0f;
                else if (shade > 1.0f)
                    shade = 1.0f;
                plasma::Vector<3, float> leaf = color * (1.0f - shade) + lightColor * shade;
                for (int c = 0; c < 3; c++) {
                    if (leaf[c] < 0.0f)
                        leaf[c] = 0.0f;
                    if (leaf[c] > 255.0f)
                        leaf[c] = 255.0f;
                }
                setBlock(i, j, k,
                         Block(plasma::Vector<3, unsigned char>(leaf), BlockLeaves | (1 << BlockFlagDestructible)),
                         zone);
                if (kind == 4 && rand() % HangingVineChanceDenominator == 0) {
                    // A hanging vine.
                    int length = rand() % HangingVineLengthRange;
                    if (rand() % 4 == 0)
                        length *= 2;
                    for (int h = k - 1; length > 0; length--, h--) {
                        if (getBlock(i, j, h, zone).type & (1 << BlockFlagInterior))
                            // Tree shape profile: Vine shading vertical aspect ratio.
                            continue;
                        float vineShade = (float)(((h - centerZ) / height * 1.8f + bump + 1.0) * 0.5);
                        if (vineShade < 0.0f)
                            vineShade = 0.0f;
                        else if (vineShade > 1.0f)
                            vineShade = 1.0f;
                        plasma::Vector<3, float> vine = color * (1.0f - vineShade) + lightColor * vineShade;
                        for (int c = 0; c < 3; c++) {
                            if (vine[c] < 0.0f)
                                vine[c] = 0.0f;
                            if (vine[c] > 255.0f)
                                vine[c] = 255.0f;
                        }
                        setBlock(
                            i, j, h,
                            Block(plasma::Vector<3, unsigned char>(vine), BlockLeaves | (1 << BlockFlagDestructible)),
                            zone);
                    }
                }
            }
        }
    }
}

// A region cell with its squared distance from a zone; sorting compares the integer distance.
struct CellDistance {
    plasma::Vector<2, int> cell;
    int distanceSquared;
    CellDistance(const plasma::Vector<2, int> &cell, int distanceSquared)
        : cell(cell),
          distanceSquared(distanceSquared)
    {}
};

// A zone of a square with its weight for a mission (the code sort instance compares the weight
// alone, largest first).
struct WeightedZone {
    plasma::Vector<2, int> zone;
    float weight;
    WeightedZone(const plasma::Vector<2, int> &zone, float weight)
        : zone(zone),
          weight(weight)
    {}
};

namespace {
enum {
    InvasionKillCountRange = 8
};
enum {
    InvasionMinimumKills = 25
};
enum {
    CaveInvasionKillCountRange = 5
};
enum {
    CaveInvasionMinimumKills = 10
};
enum {
    RiverInvasionMinimumKills = 15
};
enum {
    GangKillCountRange = 5
};
enum {
    GangMinimumKills = 6
};
enum {
    DefaultMissionKillCountRange = 10
};
enum {
    DefaultMissionMinimumKills = 10
};
}

// Gives an 8 x 8 zone square (x, y) a new mission (only for a named world): the creatures of its
// previous mission (AppearanceFlagTinted, home in the square) are deleted, its zones lose their
// added spawns, and the kind is drawn from the square's scenery: 1 (its monster) where it has
// none, 9, 3 or 4 for kind 1, 5 otherwise. The kinds 7, 10 and 11 target a hostile race with a
// kill count, 1 the square's monster; the others pick the zone most inside the square's shape for
// their target race. The mission goes to the clients in updates.
void World::generateMission(int x, int y, ServerUpdates *updates)
{
    if (m_name.empty())
        return;

    std::list<std::map<long long, Creature *>::iterator> stale;
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        if (it->second->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagMissionResident)) {
            if (it->second->m_entityData.spawnSlot[0] / ZonesPerSceneryRecord == x &&
                it->second->m_entityData.spawnSlot[1] / ZonesPerSceneryRecord == y) {
                delete it->second;
                stale.push_back(it);
            }
        }
    }
    for (std::list<std::map<long long, Creature *>::iterator>::iterator it = stale.begin(); it != stale.end(); ++it)
        m_creatures.erase(*it);

    for (int zoneX = x * ZonesPerSceneryRecord; zoneX < x * ZonesPerSceneryRecord + ZonesPerSceneryRecord; zoneX++) {
        for (int zoneY = y * ZonesPerSceneryRecord; zoneY < y * ZonesPerSceneryRecord + ZonesPerSceneryRecord;
             zoneY++) {
            Zone *zone = this->zone(zoneX, zoneY);
            if (!zone)
                continue;
            for (int i = zone->m_baseSpawnCount; i < (int)zone->m_spawns.size(); i++)
                delete zone->m_spawns[i];
            zone->m_spawns.resize(zone->m_baseSpawnCount);
        }
    }

    Region::Record *record = getRecord(x, y);
    if (!record)
        return;
    record->mission.kind = MissionNone;
    record->mission.progress = MissionNotStarted;
    record->mission.killCount = 0;
    record->mission.targetKillCount = 0;
    if (record->kind == Region::Record::SceneryPortal)
        return;

    std::vector<int> kinds;
    if (record->kind == Region::Record::SceneryCastle) {
        kinds.push_back(MissionDungeon);
    } else if (record->kind == Region::Record::SceneryCity) {
        kinds.push_back(MissionVillageInvasion);
        kinds.push_back(MissionVillageMonster);
        kinds.push_back(MissionVillageRareBoss);
    } else if (record->kind == Region::Record::SceneryNone) {
        kinds.push_back(MissionMonster);
    } else {
        kinds.push_back(MissionDungeon);
    }
    int kind = kinds[rand() % kinds.size()];

    int lowestLevel = 0;
    int highestLevel = 0;
    std::list<Creature *> players;
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        if (it->second && it->second->m_entityData.hostilityType == HostilityPlayer) {
            players.push_back(it->second);
            if (lowestLevel == 0 || it->second->m_entityData.level < lowestLevel)
                lowestLevel = it->second->m_entityData.level;
            if (highestLevel == 0 || it->second->m_entityData.level > highestLevel)
                highestLevel = it->second->m_entityData.level;
        }
    }

    if (kind == MissionInvasion) {
        record->mission.race = randomHostileRace(x, y, highestLevel);
        record->mission.targetKillCount = rand() % InvasionKillCountRange + InvasionMinimumKills;
        record->mission.targetZone = plasma::Vector<2, int>(-1, -1);
    } else if (kind == MissionCaveInvasion) {
        record->mission.race = randomHostileRace(x, y, highestLevel);
        record->mission.targetKillCount = rand() % CaveInvasionKillCountRange + CaveInvasionMinimumKills;
        record->mission.targetZone = plasma::Vector<2, int>(-1, -1);
    } else if (kind == MissionRiverInvasion) {
        record->mission.race = randomHostileRace(x, y, highestLevel);
        record->mission.targetKillCount = rand() % 5 + RiverInvasionMinimumKills;
        record->mission.targetZone = plasma::Vector<2, int>(-1, -1);
    } else if (kind == MissionMonster) {
        record->mission.race = record->monster.race;
        record->mission.targetZone = record->monster.zone;
    } else {
        // The square's zones weighted by how far inside its shape their centres lie (doubled on every
        // other zone for kinds 12 and 13).
        std::vector<WeightedZone> zones;
        for (int i = 0; i < ZonesPerSceneryRecord; i++) {
            for (int j = 0; j < ZonesPerSceneryRecord; j++) {
                plasma::Vector<2, int> zone(x * ZonesPerSceneryRecord + i, y * ZonesPerSceneryRecord + j);
                float inside = 1.0f - record->shapeDistance(
                                          Fixed(x * BlocksPerSceneryRecord + (BlocksPerZone / 2) + i * BlocksPerZone),
                                          Fixed(y * BlocksPerSceneryRecord + (BlocksPerZone / 2) + j * BlocksPerZone));
                float weight = 0.0f >= inside ? 0.0f : inside * inside;
                if (weight > 0.0f) {
                    if ((kind == MissionGang || kind == MissionGangBoss) && (zone[1] + zone[0]) % 2 != 0)
                        weight *= 2.0f;
                    zones.push_back(WeightedZone(zone, weight));
                }
            }
        }
        if (zones.empty())
            return;
        std::sort(zones.begin(), zones.end(),
                  [](const WeightedZone &a, const WeightedZone &b) { return a.weight > b.weight; });
        plasma::Vector<2, int> target = zones[0].zone;
        if (kind == MissionVillageMonster) {
            record->mission.race = randomMonsterRace(x, y, highestLevel);
        } else if (kind == MissionRareBoss || kind == MissionVillageRareBoss) {
            record->mission.race =
                randomCreatureRace(target[0] * BlocksPerZone + (BlocksPerZone / 2),
                                   target[1] * BlocksPerZone + (BlocksPerZone / 2), (int)record->height, false);
        } else if (kind == MissionGang || kind == MissionGangBoss) {
            record->mission.race =
                randomCreatureRace(target[0] * BlocksPerZone + (BlocksPerZone / 2),
                                   target[1] * BlocksPerZone + (BlocksPerZone / 2), (int)record->height, true);
            if (kind == MissionGang)
                record->mission.targetKillCount = rand() % GangKillCountRange + GangMinimumKills;
        } else {
            record->mission.race = randomHostileRace(x, y, highestLevel);
            record->mission.targetKillCount = rand() % DefaultMissionKillCountRange + DefaultMissionMinimumKills;
        }
        record->mission.targetZone = target;
    }

    record->mission.seed = rand();
    record->mission.rarity = record->rarity;
    record->mission.level = highestLevel;
    record->mission.kind = kind;
    ServerUpdates::MissionPacket packet;
    packet.position = plasma::Vector<2, int>(x, y);
    packet.mission = record->mission;
    updates->missions.push_back(packet);
}

namespace {
enum {
    MonsterWeaponUpgradeChanceDenominator = 20
};
enum {
    MonsterArmorUpgradeChanceDenominator = 100
};
enum {
    MonsterSecondArmorUpgradeChanceDenominator = 1000
};
}

// Brings a loaded zone's spawns up to date (only for a named world): the spawns past the zone's
// first m_baseSpawnCount go, with the creatures made from them. Where the zone holds its 8 x 8
// square's monster, the monster is added as a new spawn in the zone's centre, with a weapon and an
// armour piece of its rarity or better; any other zone is populated afresh.
void World::refreshZonePopulation(Zone *zone)
{
    if (m_name.empty())
        return;

    for (int i = zone->m_baseSpawnCount; i < (int)zone->m_spawns.size(); i++) {
        std::map<long long, Creature *>::iterator it = m_creatures.find(zone->m_spawns[i]->creatureId);
        if (it != m_creatures.end() && it->second) {
            delete it->second;
            m_creatures.erase(zone->m_spawns[i]->creatureId);
        }
        delete zone->m_spawns[i];
    }
    zone->m_spawns.resize(zone->m_baseSpawnCount);

    // The players' level range (not used further).
    int lowestLevel = 0;
    int highestLevel = 0;
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        Creature *creature = it->second;
        if (creature && creature->m_entityData.hostilityType == HostilityPlayer) {
            if (lowestLevel == 0 || creature->m_entityData.level < lowestLevel)
                lowestLevel = creature->m_entityData.level;
            if (highestLevel == 0 || creature->m_entityData.level > highestLevel)
                highestLevel = creature->m_entityData.level;
        }
    }

    Region::Record *record =
        getRecord(zone->m_position[0] / ZonesPerSceneryRecord, zone->m_position[1] / ZonesPerSceneryRecord);
    if (record && record->monster.race != MonsterState::NoRace && record->monster.zone == zone->m_position) {
        Spawn *monster = new Spawn;
        monster->hostilityType = HostilityEnemy;
        if (record->mission.kind == MissionMonster)
            monster->appearance.appearanceFlags |= (1 << AppearanceFlagMissionResident);
        monster->position = plasma::Vector<3, Fixed>(zone->m_position[0] * BlocksPerZone + (BlocksPerZone / 2),
                                                     zone->m_position[1] * BlocksPerZone + (BlocksPerZone / 2), 0);
        Field *field = getField((int)monster->position[0], (int)monster->position[1], zone);
        monster->position[2] = field->m_baseZ + field->m_size;
        monster->race = record->monster.race;
        monster->level = record->monster.level;
        monster->rarity = record->monster.rarity;

        int rarity = monster->rarity + 1;
        if (rand() % MonsterWeaponUpgradeChanceDenominator == 0)
            rarity++;
        if (rand() % MonsterArmorUpgradeChanceDenominator == 0)
            rarity++;
        if (rarity > ItemRarityYellow)
            rarity = ItemRarityYellow;
        monster->inventory.add(randomWeapon(monster->level, rarity, -1));
        rarity = monster->rarity + 1;
        if (rand() % 100 == 0)
            rarity++;
        if (rand() % MonsterSecondArmorUpgradeChanceDenominator == 0)
            rarity++;
        if (rarity > ItemRarityYellow)
            rarity = ItemRarityYellow;
        monster->inventory.add(randomArmor(monster->level, rarity, -1));

        SequentialBehavior *behavior = new SequentialBehavior;
        behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
        behavior->m_behaviors.push_back(new RandomWalkBehavior);
        monster->m_behavior = behavior;
        fillAppearance(monster->race, monster->appearance, 0);
        equip(monster, false);
        monster->creatureId = ((long long)zone->m_position[1] * GeneratedCreatureZoneRadix + zone->m_position[0]) *
                                  GeneratedCreatureIndexRadix +
                              (int)zone->m_spawns.size();
        zone->m_spawns.push_back(monster);
    } else {
        populateZone(zone, 0);
    }
}

namespace {
enum {
    MonsterTargetZoneRandomRange = 4
};
enum {
    EasyMissionChanceDenominator = 6
};
enum {
    CityMissionEligibilityChanceDenominator = 50
};
enum {
    TemperateMinimumGenerationLevel = 1
};
enum {
    TemperateMaximumGenerationLevel = 10
};
enum {
    ColdMinimumGenerationLevel = 10
};
enum {
    ColdMaximumGenerationLevel = 20
};
enum {
    DesertMinimumGenerationLevel = 15
};
enum {
    DesertMaximumGenerationLevel = 25
};
enum {
    JungleMinimumGenerationLevel = 10
};
enum {
    JungleMaximumGenerationLevel = 20
};
enum {
    FirelandsMinimumGenerationLevel = 20
};
enum {
    FirelandsMaximumGenerationLevel = 30
};
}

// Populates a loaded region for the day (not when the world saves nothing or has no name). Each of
// its 8 x 8 squares loses its mission and monster; a square without scenery gets a new monster of
// a level for its landscape, placed in one of its zones; squares with scenery the strongest player
// can manage become mission candidates. Two missions go to candidate squares (favouring the ones
// near the strongest player's level) and two to monster squares (generateMission). Then every zone
// is brought up to date.
void World::populateRegion(int x, int y, ServerUpdates *updates)
{
    if (m_client || m_name.empty())
        return;
    Region *region = m_regions[x][y];
    if (!region)
        return;

    int lowestLevel = 0;
    int highestLevel = 0;
    std::list<Creature *> players;
    for (std::map<long long, Creature *>::const_iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        if (it->second && it->second->m_entityData.hostilityType == HostilityPlayer) {
            players.push_back(it->second);
            if (lowestLevel == 0 || it->second->m_entityData.level < lowestLevel)
                lowestLevel = it->second->m_entityData.level;
            if (highestLevel == 0 || it->second->m_entityData.level > highestLevel)
                highestLevel = it->second->m_entityData.level;
        }
    }

    std::vector<plasma::Vector<2, int>> easySquares;
    std::vector<plasma::Vector<2, int>> fittingSquares;
    std::vector<plasma::Vector<2, int>> monsterSquares;
    // The region's 8 x 8 squares: 64 zones a side, 8 zones a square.
    for (int i = 0; i < SceneryRecordsPerRegionAxis; i++) {
        for (int j = 0; j < SceneryRecordsPerRegionAxis; j++) {
            plasma::Vector<2, int> square(x * ZonesPerRegion / ZonesPerSceneryRecord + i,
                                          y * ZonesPerRegion / ZonesPerSceneryRecord + j);
            Region::Record *record = getRecord(square[0], square[1]);
            if (regionAtColumn((int)record->center[0], (int)record->center[1]) != region)
                continue;

            if (record->mission.kind != MissionNone) {
                record->mission.kind = MissionNone;
                record->mission.killCount = 0;
                ServerUpdates::MissionPacket packet;
                packet.position = square;
                packet.mission = record->mission;
                updates->missions.push_back(packet);
            }
            if (record->monster.race != MonsterState::NoRace) {
                record->monster.race = MonsterState::NoRace;
                record->monster.level = 0;
                record->monster.rarity = ItemRarityWhite;
            }

            if (record->kind == Region::Record::SceneryNone) {
                RegionRecord *landscape = getRegionRecord(x, y);
                int minimumLevel = TemperateMinimumGenerationLevel;
                int maximumLevel = TemperateMaximumGenerationLevel;
                if (landscape->temperature < 0.2f) {
                    minimumLevel = ColdMinimumGenerationLevel;
                    maximumLevel = ColdMaximumGenerationLevel;
                }
                if (landscape->humidity < 0.2f && landscape->temperature > 0.8f) {
                    minimumLevel = DesertMinimumGenerationLevel;
                    maximumLevel = DesertMaximumGenerationLevel;
                }
                if (landscape->humidity > 0.8f && landscape->temperature > 0.8f) {
                    minimumLevel = JungleMinimumGenerationLevel;
                    maximumLevel = JungleMaximumGenerationLevel;
                }
                if (landscape->firelands == 1) {
                    minimumLevel = FirelandsMinimumGenerationLevel;
                    maximumLevel = FirelandsMaximumGenerationLevel;
                }
                record->monster.level = rand() % (maximumLevel - minimumLevel + 1) + minimumLevel;
                record->monster.rarity = ItemRarityWhite;
                record->monster.race = randomMonsterRace(x, y, record->monster.level);
                int zoneY = square[1] * ZonesPerSceneryRecord + 2 + rand() % MonsterTargetZoneRandomRange;
                int zoneX = square[0] * ZonesPerSceneryRecord + 2 + rand() % MonsterTargetZoneRandomRange;
                record->monster.zone = plasma::Vector<2, int>(zoneX, zoneY);
                monsterSquares.push_back(square);
            }
            if (record->kind != Region::Record::SceneryNone && record->kind != Region::Record::SceneryPortal &&
                record->level <= highestLevel + 2) {
                if (record->kind != Region::Record::SceneryCity ||
                    rand() % CityMissionEligibilityChanceDenominator == 0)
                    if (record->level < highestLevel - 2)
                        easySquares.push_back(square);
                    else
                        fittingSquares.push_back(square);
            }
        }
    }

    // Two missions: a fitting one, except one time in six (or when none fits) an easy one.
    for (int i = 0; i < 2; i++) {
        if ((rand() % EasyMissionChanceDenominator != 0 || easySquares.empty()) && !fittingSquares.empty()) {
            int index = rand() % fittingSquares.size();
            plasma::Vector<2, int> square = fittingSquares[index];
            fittingSquares.erase(fittingSquares.begin() + index);
            generateMission(square[0], square[1], updates);
        } else if (!easySquares.empty()) {
            int index = rand() % easySquares.size();
            plasma::Vector<2, int> square = easySquares[index];
            easySquares.erase(easySquares.begin() + index);
            generateMission(square[0], square[1], updates);
        }
    }
    for (int i = 0; i < 2 && !monsterSquares.empty(); i++) {
        int index = rand() % monsterSquares.size();
        plasma::Vector<2, int> square = monsterSquares[index];
        monsterSquares.erase(monsterSquares.begin() + index);
        generateMission(square[0], square[1], updates);
    }

    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            if (region->m_zones[i][j])
                refreshZonePopulation(region->m_zones[i][j]);
        }
    }
    region->m_populated = true;
}

namespace {
enum {
    RegionSeedRange = 10000
};
enum {
    CityRadiusRandomRange = 200
};
enum {
    DungeonSeedRange = 10000000
};
enum {
    ScenerySeedIncrementRange = 50
};
enum {
    MaximumPortalRecords = 5
};
enum {
    RegionRarityLevelThreshold = 4
};
enum {
    RegionRarityChoiceCount = 5
};
enum {
    SceneryLevelProgressionDivisor = 64
};
enum {
    DungeonWhiteRarityLevelLimit = 5
};
enum {
    DungeonGreenRarityLevelLimit = 10
};
enum {
    DungeonBlueRarityLevelLimit = 15
};
enum {
    DungeonMidlevelRarityRange = 3
};
enum {
    DungeonPurpleRarityLevelLimit = 19
};
enum {
    DungeonHighlevelRarityRange = 4
};
enum {
    SceneryKindChoiceCount = 8
};
enum {
    RuinsRadiusRandomRange = 256
};
enum {
    SceneryMidlevelRarityRange = 3
};
enum {
    SceneryHighlevelRarityRange = 4
};
enum {
    CityQuarterCount = 4
};
enum {
    RuinsCenterEdgeMarginBlocks = 256
};
enum {
    CityMinimumRadius = 512
};
enum {
    CityCenterEdgeMarginBlocks = 256
};
enum {
    RuinsMinimumRadius = 256
};
}

// Builds a region's scenery records and special zones, then restores saved mission and monster
// state. Region coordinates cover 64 x 64 zones.
Region *World::generateRegion(int x, int y)
{
    if (x < 0 || y < 0 || x >= 1024 || y >= 1024)
        return 0;
    if (m_regions[x][y])
        return m_regions[x][y];
    for (int i = -2; i <= 2; i++)
        for (int j = -2; j <= 2; j++)
            generateRegionRecord(x + i, y + j);
    srand(m_generationSeed + y * RegionsPerWorld + x);
    Region *region = new Region;
    region->m_level = regionLevel(x, y);
    region->m_rarity = ItemRarityWhite;
    if (region->m_level > RegionRarityLevelThreshold)
        region->m_rarity = rand() % RegionRarityChoiceCount;
    region->m_style = rand() % 4;
    int seed = rand() % RegionSeedRange;
    RegionRecord *landscape = m_regionRecords[x][y];
    if (landscape->temperature > 0.81f)
        region->m_style = CityClay + rand() % 2;
    int minimum = 1;
    int maximum;
    landscape->generationRange(minimum, maximum);
    std::vector<ScoredRegionCell> candidates;
    plasma::Vector<2, int> origin(landscape->column[0] / BlocksPerSceneryRecord % SceneryRecordsPerRegionAxis,
                                  landscape->column[1] / BlocksPerSceneryRecord % SceneryRecordsPerRegionAxis);
    int baseX = x * BlocksPerRegion;
    int baseY = y * BlocksPerRegion;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            Region::Record &record = region->m_records[i * 8 + j];
            if (!m_name.empty() && i == origin[0] && j == origin[1]) {
                record.kind = Region::Record::SceneryCity;
                record.style = region->m_style;
                record.seed = landscape->seed;
                record.radius = (float)(rand() % CityRadiusRandomRange + CityMinimumRadius);
                record.center = plasma::Vector<2, Fixed>(landscape->column);
                if (record.center[0] - Fixed(record.radius) - Fixed(CityCenterEdgeMarginBlocks) <
                    Fixed(baseX + i * BlocksPerSceneryRecord))
                    record.center[0] = Fixed(baseX + i * BlocksPerSceneryRecord + record.radius + 256.0f);
                if (record.center[1] - Fixed(record.radius) - Fixed(256) < Fixed(baseY + j * BlocksPerSceneryRecord))
                    record.center[1] = Fixed(baseY + j * BlocksPerSceneryRecord + record.radius + 256.0f);
                if (record.center[0] + Fixed(record.radius) + Fixed(256) >
                    Fixed(baseX + (i + 1) * BlocksPerSceneryRecord))
                    record.center[0] = Fixed(baseX + (i + 1) * BlocksPerSceneryRecord - record.radius - 256.0f);
                if (record.center[1] + Fixed(record.radius) + Fixed(256) >
                    Fixed(baseY + (j + 1) * BlocksPerSceneryRecord))
                    record.center[1] = Fixed(baseY + (j + 1) * BlocksPerSceneryRecord - record.radius - 256.0f);
                record.height = terrainHeight((int)record.center[0], (int)record.center[1], 0);
                if (record.height < 0.0f)
                    record.height = 0.0f;
            } else if (nearestRegionRecord(baseX + i * BlocksPerSceneryRecord + (BlocksPerSceneryRecord / 2),
                                           baseY + j * BlocksPerSceneryRecord + (BlocksPerSceneryRecord / 2)) ==
                       landscape) {
                record.radius = (float)(rand() % 256 + 512);
                Fixed positionY =
                    Fixed(baseY) + Fixed(j * BlocksPerSceneryRecord) + Fixed(record.radius) + Fixed(256) +
                    Fixed(rand() * (float(BlocksPerSceneryRecord) - (record.radius + 256.0f) * 2.0f) / float(RAND_MAX));
                record.center[0] =
                    Fixed(baseX) + Fixed(i * BlocksPerSceneryRecord) + Fixed(record.radius + 256.0f) +
                    Fixed(rand() * (float(BlocksPerSceneryRecord) - (record.radius + 256.0f) * 2.0f) / float(RAND_MAX));
                record.center[1] = positionY;
                record.height = terrainHeight((int)record.center[0], (int)record.center[1], 0);
                record.level = minimum;
                if (nearestRegionRecord((int)record.center[0], (int)record.center[1]) == landscape) {
                    float distance = plasma::Vector<2, float>(origin - plasma::Vector<2, int>(i, j)).length();
                    candidates.push_back(ScoredRegionCell(distance + rand() * 4.0f / float(RAND_MAX) - 2.0f,
                                                          plasma::Vector<2, int>(i, j)));
                }
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const ScoredRegionCell &a, const ScoredRegionCell &b) { return a.score < b.score; });
    for (int i = 0; i < 64 && !candidates.empty(); i++) {
        if (i % 2 != 0)
            continue;
        plasma::Vector<2, int> cell = candidates.front().cell;
        candidates.erase(candidates.begin());
        Region::Record &record = region->m_records[cell[0] * 8 + cell[1]];
        if (record.height < 0.0f)
            record.height = 0.0f;
        record.level = (int)levelFromBonus(i * (1.0f / SceneryLevelProgressionDivisor));
        if ((i / 2) % 2 != 0) {
            record.kind = Region::Record::SceneryCastle;
            record.radius = 150.0f;
            record.center[1] = Fixed((int)record.center[1] / BlocksPerZone * BlocksPerZone + (BlocksPerZone / 2));
            record.center[0] = Fixed((int)record.center[0] / 256 * 256 + (BlocksPerZone / 2));
            int zoneX = (int)Fixed((long long)((double)record.center[0].raw * (1.0 / BlocksPerZone))) % ZonesPerRegion;
            int zoneY = (int)Fixed((long long)((double)record.center[1].raw * (1.0 / BlocksPerZone))) % ZonesPerRegion;
            Region::Cell &zone = region->m_cells[zoneX * 64 + zoneY];
            zone.kind = Region::Cell::StructureDungeon;
            zone.style = rand() % (Region::Record::DungeonPalace - Region::Record::DungeonCastle + 1);
            int blockX = (x * ZonesPerRegion + zoneX) * BlocksPerZone + (BlocksPerZone / 2);
            int blockY = (y * ZonesPerRegion + zoneY) * BlocksPerZone + (BlocksPerZone / 2);
            if (blendedTemperature(blockX, blockY) > 0.8f)
                zone.style = blendedHumidity(blockX, blockY) > 0.8f ? int(Region::Record::DungeonTemple)
                                                                    : int(Region::Record::DungeonPyramid);
            zone.seed = rand() % DungeonSeedRange + 1;
            zone.level = record.level;
            record.seed = seed;
            seed += rand() % ScenerySeedIncrementRange + 1;
            record.style = zone.style;
            if (record.level < DungeonWhiteRarityLevelLimit)
                zone.rarity = ItemRarityWhite;
            else if (record.level < DungeonGreenRarityLevelLimit)
                zone.rarity = rand() % 2;
            else if (record.level < DungeonBlueRarityLevelLimit)
                zone.rarity = rand() % DungeonMidlevelRarityRange;
            else if (record.level < DungeonPurpleRarityLevelLimit)
                zone.rarity = rand() % 3 + 1;
            else
                zone.rarity = rand() % DungeonHighlevelRarityRange + 1;
        } else {
            switch (rand() % SceneryKindChoiceCount) {
            case 0:
                record.kind = Region::Record::SceneryMountain;
                break;
            case 1:
                record.kind = Region::Record::SceneryForest;
                record.style = rand() % 3;
                break;
            case 2:
                record.kind =
                    landscape->height < 0 ? int(Region::Record::SceneryIsland) : int(Region::Record::SceneryLake);
                break;
            case 3:
                record.kind = Region::Record::SceneryRuins;
                if (landscape->temperature > 0.8f && landscape->humidity > 0.8f)
                    record.style = RuinJungle;
                else if (landscape->temperature > 0.8f && landscape->humidity < 0.2f)
                    record.style = RuinDesert;
                else
                    record.style = RuinAntique;
                record.radius = (float)(rand() % RuinsRadiusRandomRange + RuinsMinimumRadius);
                {
                    Fixed positionY = Fixed(baseY) + Fixed(cell[1] * BlocksPerSceneryRecord) + Fixed(record.radius) +
                                      Fixed(RuinsCenterEdgeMarginBlocks) +
                                      Fixed(rand() * (float(BlocksPerSceneryRecord) - (record.radius + 256.0f) * 2.0f) /
                                            float(RAND_MAX));
                    record.center[0] =
                        Fixed(baseX) + Fixed(cell[0] * BlocksPerSceneryRecord) + Fixed(record.radius + 256.0f) +
                        Fixed(rand() * (float(BlocksPerSceneryRecord) - (record.radius + 256.0f) * 2.0f) /
                              float(RAND_MAX));
                    record.center[1] = positionY;
                }
                break;
            case 4:
                record.kind =
                    landscape->height < 0 ? int(Region::Record::SceneryIsland) : int(Region::Record::SceneryCanyon);
                break;
            case 5:
                record.kind =
                    landscape->height < 0 ? int(Region::Record::SceneryIsland) : int(Region::Record::SceneryValley);
                break;
            case 6:
                record.kind = Region::Record::SceneryRock;
                record.radius = 128.0f;
                record.center[1] = Fixed((int)record.center[1] / 256 * 256 + (BlocksPerZone / 2));
                record.center[0] = Fixed((int)record.center[0] / 256 * 256 + (BlocksPerZone / 2));
                break;
            case 7:
                record.kind = Region::Record::SceneryTree;
                record.radius = 128.0f;
                record.center[1] = Fixed((int)record.center[1] / 256 * 256 + (BlocksPerZone / 2));
                record.center[0] = Fixed((int)record.center[0] / 256 * 256 + (BlocksPerZone / 2));
                break;
            }
            if (record.level < 5)
                record.rarity = ItemRarityWhite;
            else if (record.level < 10)
                record.rarity = rand() % 2;
            else if (record.level < 15)
                record.rarity = rand() % SceneryMidlevelRarityRange;
            else if (record.level < 19)
                record.rarity = rand() % 3 + 1;
            else
                record.rarity = rand() % SceneryHighlevelRarityRange + 1;
            record.seed = seed;
            seed += rand() % 50 + 1;
        }
    }
    for (int i = 0; i < MaximumPortalRecords && !candidates.empty(); i++) {
        unsigned int index = rand() % candidates.size();
        plasma::Vector<2, int> cell = candidates[index].cell;
        candidates.erase(candidates.begin() + index);
        Region::Record &record = region->m_records[cell[0] * 8 + cell[1]];
        record.kind = Region::Record::SceneryPortal;
        record.seed = rand() % DungeonSeedRange + 1;
        int zoneX = (int)record.center[0] / BlocksPerZone % ZonesPerRegion;
        int zoneY = (int)record.center[1] / BlocksPerZone % ZonesPerRegion;
        Region::Cell &zone = region->m_cells[zoneX * 64 + zoneY];
        zone.kind = Region::Cell::StructurePortal;
        zone.seed = record.seed;
    }
    for (int i = 0; i < 64; i++) {
        Region::Record &record = region->m_records[i];
        int zoneX = (int)record.center[0] / BlocksPerZone % ZonesPerRegion;
        int zoneY = (int)record.center[1] / BlocksPerZone % ZonesPerRegion;
        region->m_cells[zoneX * 64 + zoneY].level = record.level;
    }
    // The code samples the four corners and collects these zone coordinates even though it does not
    // consume the resulting vector.
    std::vector<plasma::Vector<2, int>> eligibleZones;
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            terrainHeight(baseX + i * BlocksPerZone, baseY + j * BlocksPerZone, 0);
            terrainHeight(baseX + i * BlocksPerZone + BlocksPerZone, baseY + j * BlocksPerZone, 0);
            terrainHeight(baseX + i * BlocksPerZone, baseY + j * BlocksPerZone + BlocksPerZone, 0);
            terrainHeight(baseX + i * BlocksPerZone + BlocksPerZone, baseY + j * BlocksPerZone + BlocksPerZone, 0);
            if ((x * 64 + i + (y * 64 + j) * 5) % 13 == 0)
                eligibleZones.push_back(plasma::Vector<2, int>(i, j));
        }
    }
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            Region::Record &record = region->m_records[i * 8 + j];
            if (record.kind != Region::Record::SceneryCity)
                continue;
            std::vector<ScoredRegionCell> zones;
            for (int zoneX = 0; zoneX < ZonesPerSceneryRecord; zoneX++) {
                for (int zoneY = 0; zoneY < 8; zoneY++) {
                    float weight =
                        1.0f - record.shapeDistance(
                                   Fixed((x * ZonesPerSceneryRecord + i) * BlocksPerSceneryRecord +
                                         (BlocksPerZone / 2) + zoneX * BlocksPerZone),
                                   Fixed((y * ZonesPerSceneryRecord + j) * 2048 + 128 + zoneY * BlocksPerZone));
                    if (weight <= 0.0f)
                        weight = 0.0f;
                    else
                        weight *= weight;
                    zones.push_back(
                        ScoredRegionCell(weight, plasma::Vector<2, int>(zoneX + i * ZonesPerSceneryRecord,
                                                                        zoneY + j * ZonesPerSceneryRecord)));
                }
            }
            std::sort(zones.begin(), zones.end(),
                      [](const ScoredRegionCell &a, const ScoredRegionCell &b) { return a.score > b.score; });
            for (int k = 0; k < CityQuarterCount; k++) {
                Region::Cell &zone = region->m_cells[zones[k].cell[0] * 64 + zones[k].cell[1]];
                zone.kind = Region::Cell::StructureQuarter;
                zone.style = k + Region::Cell::DistrictTrade;
            }
        }
    }
    if (!m_client && !m_name.empty()) {
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                Region::Record &record = region->m_records[i * 8 + j];
                {
                    std::stringstream key;
                    key << "mission" << x * ZonesPerSceneryRecord + i << "_" << y * ZonesPerSceneryRecord + j;
                    BytesIO savedMissionData;
                    if (m_database.read(key.str(), savedMissionData)) {
                        Serializer(&savedMissionData).loadMission(record.mission);
                        region->m_populated = true;
                    }
                }
                {
                    std::stringstream key;
                    key << "monster" << x * ZonesPerSceneryRecord + i << "_" << y * ZonesPerSceneryRecord + j;
                    BytesIO savedMonsterData;
                    if (m_database.read(key.str(), savedMonsterData))
                        Serializer(&savedMonsterData).loadMonster(record.monster);
                }
            }
        }
    }
    m_regions[x][y] = region;
    return region;
}

namespace {
enum {
    CampfireSettleSearchWidth = 3
};
enum {
    CampFollowerCountRange = 3
};
enum {
    EncounterFollowerCountRange = 3
};
enum {
    HumanoidClassChoiceCount = 4
};
enum {
    MageWeaponLoadoutChoiceCount = 3
};
}

// Puts creatures and camps on a region record's places in a zone (the zone generator passes the
// places it found on a grid inside the record's shape; count is never read). The record's kind
// offers a list of themes and its picks one. A theme gives the races of the creatures that stand
// about a place and the group styles (Dungeon:Style: a leader's races, then its followers');
// themes 1..5 and the default also light a camp fire. Each place is used at random, except the one
// the record weighs most on when its centre lies in this zone.
void World::populateRecord(Zone *zone, Region::Record *record, std::vector<plasma::Vector<3, Fixed>> &places, int count)
{
    if (record->kind == Region::Record::SceneryNone)
        return;
    std::vector<Dungeon::Style> styles;
    std::vector<int> races;
    bool camp = false;
    std::vector<int> themes;
    if (record->kind == Region::Record::SceneryLake) {
        themes.push_back(EncounterLakeCreatures);
    } else {
        themes.push_back(EncounterOrcs);
        themes.push_back(EncounterUndead);
        themes.push_back(EncounterHumans);
        themes.push_back(EncounterDwarves);
        themes.push_back(EncounterLizardmen);
        themes.push_back(EncounterGoblins);
        themes.push_back(EncounterUndead);
        themes.push_back(EncounterInsectoids);
        themes.push_back(EncounterBeetles);
        if (record->kind != Region::Record::SceneryPeak && record->kind != Region::Record::SceneryMountain)
            themes.push_back(EncounterPlantCreatures);
        if (record->kind == Region::Record::SceneryForest)
            themes.push_back(EncounterForestAnimals);
    }
    switch (themes[record->seed % themes.size()]) {
    case EncounterUndead: {
        camp = true;
        races.push_back(RaceUndeadMale);
        races.push_back(RaceUndeadFemale);
        Dungeon::Style style;
        switch (rand() % HumanoidClassChoiceCount) {
        case 0:
            style.guardRaces.push_back(RaceSkeleton);
            break;
        case 1:
            style.guardRaces.push_back(RaceFrightener);
            break;
        case 2:
            style.guardRaces.push_back(RaceVampire);
            break;
        }
        switch (rand() % 4) {
        case 0:
            style.followerRaces.push_back(RaceBlueSlime);
            style.followerRaces.push_back(RaceGreenSlime);
            style.followerRaces.push_back(RacePinkSlime);
            style.followerRaces.push_back(RaceYellowSlime);
            break;
        case 1:
            style.followerRaces.push_back(RaceBat);
            break;
        case 2:
            style.followerRaces.push_back(RaceFrightener);
            break;
        case 3:
            style.followerRaces.push_back(RaceZombie);
            break;
        }
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceUndeadMale);
        style.guardRaces.push_back(RaceUndeadFemale);
        style.followerRaces.push_back(RaceUndeadMale);
        style.followerRaces.push_back(RaceUndeadFemale);
        styles.push_back(style);
        break;
    }
    case EncounterHumans: {
        camp = true;
        races.push_back(RaceHumanMale);
        races.push_back(RaceHumanFemale);
        Dungeon::Style style;
        switch (rand() % MageWeaponLoadoutChoiceCount) {
        case 0:
            style.guardRaces.push_back(RaceWerewolf);
            break;
        case 1:
            style.guardRaces.push_back(RaceJester);
            break;
        case 2:
            style.guardRaces.push_back(RaceMinotaur);
            break;
        }
        // Cases 4 and 5 are never reached: the code draws rand % 4.
        switch (rand() % 4) {
        case 0:
            style.followerRaces.push_back(RaceCat);
            break;
        case 1:
            style.followerRaces.push_back(RaceTerrier);
            break;
        case 2:
            style.followerRaces.push_back(RaceCollie);
            break;
        case 3:
            style.followerRaces.push_back(RacePig);
            break;
        case 4:
            style.followerRaces.push_back(RaceBrownCat);
            break;
        case 5:
            style.followerRaces.push_back(RaceWhiteCat);
            break;
        }
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceHumanMale);
        style.guardRaces.push_back(RaceHumanFemale);
        style.followerRaces.push_back(RaceHumanMale);
        style.followerRaces.push_back(RaceHumanFemale);
        styles.push_back(style);
        break;
    }
    case EncounterDwarves: {
        camp = true;
        races.push_back(RaceDwarfMale);
        races.push_back(RaceDwarfFemale);
        Dungeon::Style style;
        switch (rand() % 2) {
        case 0:
            style.guardRaces.push_back(RaceRockling);
            break;
        case 1:
            style.guardRaces.push_back(RaceBiter);
            break;
        }
        switch (rand() % 3) {
        case 0:
            style.followerRaces.push_back(RaceMole);
            break;
        case 1:
            style.followerRaces.push_back(RaceRaccoon);
            break;
        case 2:
            style.followerRaces.push_back(RacePig);
            break;
        }
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceDwarfMale);
        style.guardRaces.push_back(RaceDwarfFemale);
        style.followerRaces.push_back(RaceDwarfMale);
        style.followerRaces.push_back(RaceDwarfFemale);
        styles.push_back(style);
        break;
    }
    case EncounterLizardmen: {
        camp = true;
        races.push_back(RaceLizardmanMale);
        races.push_back(RaceLizardmanFemale);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceLizardmanMale);
        style.guardRaces.push_back(RaceLizardmanFemale);
        switch (rand() % 3) {
        case 0:
            style.followerRaces.push_back(RaceFly);
            break;
        case 1:
            style.followerRaces.push_back(RaceHornet);
            break;
        case 2:
            style.followerRaces.push_back(RaceParrot);
            break;
        }
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceLizardmanMale);
        style.guardRaces.push_back(RaceLizardmanFemale);
        style.followerRaces.push_back(RaceLizardmanMale);
        style.followerRaces.push_back(RaceLizardmanFemale);
        styles.push_back(style);
        break;
    }
    case EncounterGoblins: {
        camp = true;
        races.push_back(RaceGoblinMale);
        races.push_back(RaceGoblinFemale);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceBiter);
        style.followerRaces.push_back(RaceMole);
        style.followerRaces.push_back(RaceCrow);
        style.followerRaces.push_back(RaceFly);
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceGoblinMale);
        style.guardRaces.push_back(RaceGoblinFemale);
        style.followerRaces.push_back(RaceGoblinMale);
        style.followerRaces.push_back(RaceGoblinFemale);
        styles.push_back(style);
        break;
    }
    case EncounterPlantCreatures: {
        races.push_back(RaceRadishCreature);
        races.push_back(RacePlantCreature);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceOnionling);
        style.followerRaces.push_back(RaceOnionling);
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceRadishCreature);
        style.guardRaces.push_back(RacePlantCreature);
        style.followerRaces.push_back(RaceRadishCreature);
        style.followerRaces.push_back(RacePlantCreature);
        styles.push_back(style);
        break;
    }
    case EncounterInsectoids: {
        races.push_back(RaceInsectoid);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceMosquito);
        style.followerRaces.push_back(RaceMosquito);
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceFly);
        style.followerRaces.push_back(RaceFly);
        styles.push_back(style);
        // Only the followers are cleared: the third style's leaders are races 60 and 52.
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceInsectoid);
        style.followerRaces.push_back(RaceInsectoid);
        styles.push_back(style);
        break;
    }
    case EncounterBeetles: {
        races.push_back(RaceBarkBeetle);
        races.push_back(RaceLemonBeetle);
        races.push_back(RaceSnoutBeetle);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceSnoutBeetle);
        style.followerRaces.push_back(RaceSnoutBeetle);
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceBarkBeetle);
        style.followerRaces.push_back(RaceBarkBeetle);
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceLemonBeetle);
        style.followerRaces.push_back(RaceLemonBeetle);
        styles.push_back(style);
        break;
    }
    case EncounterLakeCreatures: {
        races.push_back(RaceSpitter);
        races.push_back(RaceCrab);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceSpitter);
        style.followerRaces.push_back(RaceSpitter);
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceCrab);
        style.followerRaces.push_back(RaceCrab);
        styles.push_back(style);
        break;
    }
    case EncounterForestAnimals: {
        races.push_back(RaceBunny);
        races.push_back(RaceSquirrel);
        races.push_back(RaceRaccoon);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceBunny);
        style.followerRaces.push_back(RaceBunny);
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceSquirrel);
        style.followerRaces.push_back(RaceSquirrel);
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceRaccoon);
        style.followerRaces.push_back(RaceRaccoon);
        styles.push_back(style);
        break;
    }
    default: {
        camp = true;
        races.push_back(RaceOrcMale);
        races.push_back(RaceOrcFemale);
        Dungeon::Style style;
        style.guardRaces.push_back(RaceOgre);
        style.followerRaces.push_back(RaceCollie);
        style.followerRaces.push_back(RacePig);
        style.followerRaces.push_back(RaceTerrier);
        styles.push_back(style);
        style.guardRaces.clear();
        style.followerRaces.clear();
        style.guardRaces.push_back(RaceOrcMale);
        style.guardRaces.push_back(RaceOrcFemale);
        style.followerRaces.push_back(RaceOrcMale);
        style.followerRaces.push_back(RaceOrcFemale);
        styles.push_back(style);
        break;
    }
    }

    // The place the record weighs most on (its squared influence), when the record's centre lies in
    // this zone.
    int chosen = -1;
    int centerX = (int)record->center[0];
    int centerY = (int)record->center[1];
    if (plasma::Vector<2, int>(centerX / BlocksPerZone, centerY / BlocksPerZone) == zone->m_position) {
        int placeCount = (int)places.size();
        float best = 0.0f;
        for (int i = 0; i < placeCount; i++) {
            float weight = 1.0f - record->shapeDistance(places[i][0], places[i][1]);
            weight = weight > 0.0f ? weight * weight : 0.0f;
            if (weight > best) {
                best = weight;
                chosen = i;
            }
        }
    }

    for (int i = 0; i < (int)places.size(); i++) {
        plasma::Vector<3, Fixed> place = places[i];
        // The other places 5 to 128 blocks away: a group leader walks between some of them.
        std::vector<plasma::Vector<3, Fixed>> nearby;
        for (std::vector<plasma::Vector<3, Fixed>>::iterator it = places.begin(); it != places.end(); ++it) {
            plasma::Vector<3, float> offset(*it - place);
            float distance = plasma::Vector<2, float>(offset[0], offset[1]).squaredLength();
            if (distance > 25.0f && distance < 16384.0f)
                nearby.push_back(*it);
        }
        if (i != chosen && rand() % 2 != 0)
            continue;
        if ((i == chosen || rand() % 2 != 0) && !races.empty()) {
            if (camp) {
                // A camp fire on the first spot of a 3 x 3 square where it settles; the place moves to it. Four
                // objects stand at the corners 3.5 blocks around.
                Zone::Object fire;
                // Object layout: per-axis dimensions and placement offsets for this model.
                fire.type = StaticObjectCampfire;
                fire.size = plasma::Vector<3, float>(2.4f, 2.4f, 0.5f);
                fire.rotation = rand() % 4;
                for (int x = 0; x < CampfireSettleSearchWidth; x++) {
                    for (int y = 0; y < 3; y++) {
                        fire.position = plasma::Vector<3, Fixed>(place[0] + Fixed(x), place[1] + Fixed(y), place[2]);
                        if (settleObject(fire, zone, true)) {
                            zone->m_objects.push_back(fire);
                            place = fire.position;
                            goto firePlaced;
                        }
                    }
                }
            firePlaced:
                for (int x = 0; x < 14; x += 7) {
                    for (int y = 0; y < 14; y += 7) {
                        Zone::Object object;
                        switch (rand() % 4) {
                        case 1:
                            object.type = StaticObjectStool;
                            object.size = plasma::Vector<3, float>(1.0f, 1.0f, 0.5f);
                            break;
                        case 2:
                            // Camp furniture layout: stool, bedroll and mat model dimensions.
                            object.type = StaticObjectTable;
                            object.size = plasma::Vector<3, float>(3.0f, 3.0f, 1.0f);
                            break;
                        case 3:
                            object.type = StaticObjectSleepingMat;
                            object.size = plasma::Vector<3, float>(2.0f, 2.0f, 0.1f);
                            break;
                        default:
                            object.type = StaticObjectTent;
                            object.size = plasma::Vector<3, float>(4.0f, 4.0f, 3.0f);
                            break;
                        }
                        object.rotation = rand() % 4;
                        object.position = plasma::Vector<3, Fixed>(place[0] + (Fixed(x) - Fixed(3.5)),
                                                                   place[1] + (Fixed(y) - Fixed(3.5)), place[2]);
                        if (settleObject(object, zone, true))
                            zone->m_objects.push_back(object);
                    }
                }
            }
            // One to three creatures of the theme's races in a circle of three blocks around the place, facing
            // outwards.
            float start = (float)(rand() * 6.283185307179586 / double(RAND_MAX));
            int n = rand() % CampFollowerCountRange + 1;
            for (int k = 0; k < n; k++) {
                float angle = (float)(k * 2 * 3.141592653589793 / n + start);
                Spawn *spawn = new Spawn;
                spawn->hostilityType = HostilityEnemy;
                spawn->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                spawn->rotation = (float)(angle * 180.0f / 3.141592653589793 + 90.0);
                spawn->position = plasma::Vector<3, Fixed>(place[0] + Fixed(cos(angle) * 3.0f),
                                                           place[1] + Fixed(sin(angle) * 3.0f), place[2]);
                spawn->race = races[rand() % races.size()];
                spawn->level = record->level;
                spawn->rarity = (unsigned char)record->rarity;
                SequentialBehavior *behavior = new SequentialBehavior;
                Behavior *combat = new CombatBehavior(20.0f);
                behavior->m_behaviors.push_back(combat);
                WalkPathBehavior *home = new WalkPathBehavior(2.0f);
                home->m_path.push_back(spawn->position);
                behavior->m_behaviors.push_back(home);
                if (i == chosen && k == 0) {
                    spawn->appearance.appearanceFlags |= (1 << AppearanceFlagTinted);
                    spawn->missionResident = true;
                }
                Behavior *interaction = new RandomInteractionBehavior;
                behavior->m_behaviors.push_back(interaction);
                Behavior *wander = new RandomWalkBehavior;
                behavior->m_behaviors.push_back(wander);
                spawn->m_behavior = behavior;
                fillAppearance(spawn->race, spawn->appearance, 0);
                equip(spawn, false);
                zone->m_spawns.push_back(spawn);
            }
        } else if (!styles.empty()) {
            // A group: a leader walking from the place to up to three nearby places, and one to three
            // followers.
            int index = rand() % styles.size();
            if (styles[index].guardRaces.empty())
                continue;
            Spawn *leader = new Spawn;
            leader->hostilityType = HostilityEnemy;
            leader->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
            leader->rotation = (float)(rand() % 360);
            leader->position = place;
            leader->race = styles[index].guardRaces[rand() % styles[index].guardRaces.size()];
            leader->level = record->level;
            leader->rarity = (unsigned char)record->rarity;
            SequentialBehavior *behavior = new SequentialBehavior;
            Behavior *combat = new CombatBehavior(20.0f);
            behavior->m_behaviors.push_back(combat);
            WalkPathBehavior *walk = new WalkPathBehavior(2.0f);
            walk->m_path.push_back(leader->position);
            behavior->m_behaviors.push_back(walk);
            if (!nearby.empty()) {
                for (int k = 0; k < 3; k++) {
                    plasma::Vector<3, Fixed> point = nearby[rand() % nearby.size()];
                    Field *column = getField((int)point[0], (int)point[1], zone);
                    if (!column)
                        continue;
                    point[2] = column->m_groundZ;
                    while (getBlock(point[0], point[1], point[2], zone).isSolid())
                        point[2] += Fixed(1);
                    walk->m_path.push_back(point);
                }
            }
            Behavior *wander = new RandomWalkBehavior;
            behavior->m_behaviors.push_back(wander);
            leader->m_behavior = behavior;
            zone->m_spawns.push_back(leader);
            if (!styles[index].followerRaces.empty()) {
                int followers = rand() % EncounterFollowerCountRange + 1;
                for (int k = 0; k < followers; k++) {
                    Spawn *follower = new Spawn;
                    follower->position = leader->position;
                    follower->hostilityType = HostilityEnemy;
                    follower->appearance.appearanceFlags |= (1 << AppearanceFlagMissionLevel);
                    follower->race = styles[index].followerRaces[rand() % styles[index].followerRaces.size()];
                    follower->level = record->level;
                    follower->rarity = (unsigned char)record->rarity;
                    SequentialBehavior *behaviors = new SequentialBehavior;
                    Behavior *combat = new CombatBehavior(20.0f);
                    behaviors->m_behaviors.push_back(combat);
                    CompanionBehavior *companion = new CompanionBehavior;
                    companion->m_ownerId = leader->creatureId;
                    behaviors->m_behaviors.push_back(companion);
                    Behavior *wander = new RandomWalkBehavior;
                    behaviors->m_behaviors.push_back(wander);
                    follower->m_behavior = behaviors;
                    zone->m_spawns.push_back(follower);
                }
            }
        }
    }
}

// A colour clamped to 0..255 in each component, as a block colour.
static plasma::Vector<3, unsigned char> toBlockColor(plasma::Vector<3, float> color)
{
    for (int i = 0; i < 3; i++) {
        if (color[i] < 0.0f)
            color[i] = 0.0f;
        if (color[i] > 255.0f)
            color[i] = 255.0f;
    }
    return plasma::Vector<3, unsigned char>(color);
}

// Fills a box with stone of a colour, each block shaded from a small random table and tinted
// towards green where colorBlend says; rough blocks (type 13, a share given by roughness) and
// plain ones (type 6) are both interior blocks, with the given flags added.
void World::fillBox(int x, int y, int z, const plasma::Vector<3, int> &size,
                    const plasma::Vector<3, unsigned char> &color, float roughness, Zone *zone, int flags)
// Stone shade table: 23 entries, every eighth drawn from 0..19; block-coordinate hashing selects
// each shade.
{
    int shades[23];
    for (int i = 0; i < 23; i++) {
        if (i % 8 == 0)
            shades[i] = rand() % 20;
        else
            shades[i] = 0;
    }
    for (int i = 0; i < size[0]; i++) {
        for (int j = 0; j < size[1]; j++) {
            for (int k = size[2] - 1; k >= 0; k--) {
                int bx = x + i;
                int by = y + j;
                int bz = z + k;
                int shade = shades[((i + k * 7) / 2 + (j + k * 7) / 2 * 7) % 23];
                plasma::Vector<3, float> placementMargins(color[0] + shade, color[1] + shade, color[2] + shade);
                float blend = colorBlend(bx, by, bz, zone);
                placementMargins[1] = blend * (120.0f - placementMargins[1]) + placementMargins[1];
                // Read and not used.
                getBlock(bx, by, bz, zone);
                if (roughness >= rand() / float(RAND_MAX)) {
                    plasma::Vector<3, unsigned char> blockColor = toBlockColor(placementMargins);
                    Block block(blockColor, flags | (1 << BlockFlagInterior) | 13);
                    setBlock(bx, by, bz, block, zone);
                } else {
                    plasma::Vector<3, unsigned char> blockColor = toBlockColor(placementMargins);
                    Block block(blockColor, flags | (1 << BlockFlagInterior) | BlockBuilding);
                    setBlock(bx, by, bz, block, zone);
                }
            }
        }
    }
}

// 3t^2 - 2t^3: an eased step from 0 to 1.
static float smoothStep(float t)
{
    return t * 3.0f * t - t * 2.0f * t * t;
}

// A colour clamped to 0..255 in each component.
static plasma::Vector<3, float> clampChannels(plasma::Vector<3, float> color)
{
    for (int i = 0; i < 3; i++) {
        if (color[i] < 0.0f)
            color[i] = 0.0f;
        else if (color[i] > 255.0f)
            color[i] = 255.0f;
    }
    return color;
}

namespace {
enum {
    SnowTrunkDarkPatchChanceDenominator = 8
};
enum {
    BroadTreeRootCountRange = 4
};
enum {
    BroadTreeMinimumRoots = 5
};
enum {
    PalmLeafCrownDrop = 5
};
enum {
    PalmLeafCrownJitterRange = 3
};
enum {
    PalmInnerCrownJitterRange = 3
};
enum {
    PalmFruitChanceDenominator = 10
};
enum {
    PalmDiagonalFrondOmissionDenominator = 3
};
enum {
    RoundTreeBranchCountRange = 5
};
enum {
    RoundTreeMinimumBranches = 4
};
enum {
    BroadTreeBranchCountRange = 5
};
enum {
    BroadTreeMinimumBranches = 8
};
enum {
    PalmFrondFoliageStep = 4
};
enum {
    PalmFrondFoliageChanceDenominator = 3
};
}

// Raises a tree with a narrowing trunk and a crown selected by kind: rounded foliage for kinds
// zero, two and four, a conifer for one, leaning leaves for three, palm fronds for five and roots
// and branches for six. Colors follow temperature, cold trees get snow, and some produce fruit
// plants.
void World::buildTree(int x, int y, int z, int radius, int height, int kind, Zone *zone)
{
    float size = (float)radius;
    float footRadius = size * 0.25f;
    float topRadius = size * 0.25f;
    Fixed centerX = Fixed(x) - Fixed(-0.5);
    Fixed centerY = Fixed(y) - Fixed(-0.5);
    // Tree trunk and leaf palette by climate/kind.
    float temperature = terrainTemperature(x, y, zone);
    float green = temperature * 50.0f + 120.0f;
    float blue = temperature * 100.0f + 50.0f;
    // The trunk's colours at its foot and at its top.
    // The original draws g, b in that order for both colours.
    plasma::Vector<3, float> trunkFoot(240.0f, green + rand() % 80, blue + rand() % 50);
    plasma::Vector<3, float> trunkTop(240.0f, green + rand() % 80, blue + rand() % 50);
    // Trunk shape profile: palm height/radius transforms, per-kind radius overrides and minimum
    // radius.
    if (kind == 5) {
        height = (int)(height * 0.7f);
        radius = (int)(size * 0.2f);
        if (radius > 3)
            radius = 3;
        float tall = (float)(radius * 8);
        if (tall > (float)height)
            height = (int)tall;
    } else if (kind == 1) {
        footRadius *= 0.5f;
        topRadius = 0.1f;
        height *= 2;
    } else if (kind == 6) {
        topRadius = footRadius * 0.5f;
        // Procedural RGB palette and brightness coefficients for this surface.
    } else if (kind == 3) {
        trunkFoot = plasma::Vector<3, float>(240.0f, 180.0f, 120.0f);
        footRadius = 2.0f;
        trunkTop = plasma::Vector<3, float>(220.0f, 100.0f, 50.0f);
        topRadius = 2.0f;
    } else if (kind == 2) {
        footRadius = 1.5f;
        topRadius = 1.5f;
        trunkFoot = plasma::Vector<3, float>(255.0f, 255.0f, 255.0f);
        trunkTop = plasma::Vector<3, float>(255.0f, 255.0f, 255.0f);
    }
    if (footRadius < 0.3f)
        footRadius = 0.3f;
    if (topRadius < 0.3f)
        topRadius = 0.3f;

    // The foliage colours: green in warm columns (and for kinds 3 and 4), otherwise red, orange or
    // pale; fruit only with the latter.
    plasma::Vector<3, float> leaves;
    plasma::Vector<3, float> leavesLight;
    bool emitLeaves = false;
    float greenChance = (temperature - 0.5f) * 10.0f;
    if (temperature < 0.0f)
        temperature = 0.0f;
    else if (temperature > 1.0f)
        // Tree trunk and leaf palette by climate/kind.
        temperature = 1.0f;
    if (greenChance > rand() / float(RAND_MAX) || kind == 4 || kind == 3) {
        // The original draws r, g, b in that order for every leaf colour below.
        leaves = plasma::Vector<3, float>(rand() % 20, rand() % 155 + 50, rand() % 100);
        leaves = clampChannels(leaves);
        float light = rand() % 100 + 50;
        leavesLight = clampChannels(leaves + plasma::Vector<3, float>(light, light, 0.0f));
    } else if (terrainHumidity(x, y, zone) > 0.5f) {
        leaves = plasma::Vector<3, float>(rand() % 55 + 200, rand() % 100, rand() % 50);
        float light = rand() % 100 + 50;
        leavesLight = clampColor(leaves + plasma::Vector<3, float>(light, light, 0.0f));
        emitLeaves = true;
    } else {
        switch (rand() % 2) {
        case 0: {
            leaves = clampColor(plasma::Vector<3, float>(rand() % 55 + 200, 0.0f, rand() % 200));
            float light = rand() % 100 + 100;
            leavesLight = clampColor(leaves + plasma::Vector<3, float>(light, light, light));
            emitLeaves = true;
            break;
        }
        case 1: {
            leaves = clampColor(plasma::Vector<3, float>(rand() % 20, rand() % 100 + 120, rand() % 100));
            float light = rand() % 150 + 100;
            leavesLight = clampColor(leaves + plasma::Vector<3, float>(light, light, 0.0f));
            break;
        }
        }
    }
    if (kind == 2) {
        leaves = plasma::Vector<3, float>(150.0f, 255.0f, 0.0f);
        leavesLight = plasma::Vector<3, float>(244.0f, 255.0f, 0.0f);
        emitLeaves = false;
        // Tree trunk and leaf palette by climate/kind.
    }
    // Cold columns: snow on the foliage and the trunk.
    if (temperature < 0.2f) {
        float snow = 1.0f - (temperature - 0.1f) / 0.1f;
        if (snow > 1.0f)
            snow = 1.0f;
        float rest = 1.0f - snow;
        leaves = snowColor(x, y) * snow + leaves * rest;
        // Snow-covered trunk base color palette (120,120,80).
        leavesLight = snowColor(x, y) * snow + leavesLight * rest;
        trunkFoot = plasma::Vector<3, float>(120.0f, 120.0f, 80.0f) * snow + trunkFoot * rest;
    }

    // A leaning trunk's top stays inside the zone.
    float leanX = 0.0f;
    float leanY = 0.0f;
    if (kind == 3) {
        leanX = (0.5f - rand() / float(RAND_MAX)) * height;
        leanY = (0.5f - rand() / float(RAND_MAX)) * height;
        float topX = x + leanX;
        if (zone->m_position[0] * BlocksPerZone > topX - radius ||
            topX + radius >= zone->m_position[0] * BlocksPerZone + BlocksPerZone)
            leanX = 0.0f;
        float topY = y + leanY;
        if (zone->m_position[1] * BlocksPerZone > topY - radius ||
            topY + radius >= zone->m_position[1] * BlocksPerZone + BlocksPerZone)
            leanY = 0.0f;
    }

    // The trunk, column by column from its top down to two blocks below the ground; its radius blends
    // from the foot's to the top's.
    for (int i = (int)(x - footRadius - 1.0f); i <= x + footRadius + 1.0f; i++) {
        for (int j = (int)(y - footRadius - 1.0f); j <= y + footRadius + 1.0f; j++) {
            for (int k = z + height; k >= z - 2; k--) {
                float along = (k - z + 2) / (height + 2.0f);
                if (along < 0.0f)
                    along = 0.0f;
                float curve = along * along;
                float r = (1.0f - curve) * footRadius + curve * topRadius;
                int columnX = i;
                int columnY = j;
                if (kind == 3) {
                    columnX = (int)(leanX * curve + i);
                    columnY = (int)(leanY * curve + j);
                }
                // Trunk outline method threshold and gnarled-trunk noise coefficients.
                plasma::Vector<2, double> offset;
                if (r > 0.8f) {
                    double dy = toBlocks(Fixed(j) - centerY);
                    double dx = toBlocks(Fixed(i) - centerX);
                    if (kind == 6) {
                        // A gnarled trunk: the outline wanders with noise.
                        float wobble = 5.0f / r;
                        offset = plasma::Vector<2, double>(noise(j * 0.025, k * 0.025) * wobble + dx / r,
                                                           noise(i * 0.025, k * 0.025 + 8473.0) * wobble + dy / r);
                    } else {
                        offset = plasma::Vector<2, double>(dx / r, dy / r);
                    }
                } else {
                    offset = plasma::Vector<2, double>((double)(i - (int)centerX) / r, (double)(j - (int)centerY) / r);
                }
                // Inside the trunk's outline only. A height of -2 makes the blend 0/0 and the outline NaN, which
                // holds nothing.
                if (offset[1] * offset[1] + offset[0] * offset[0] <= 1.0) {
                    if (getBlock(columnX, columnY, k, zone).type & (1 << BlockFlagInterior))
                        continue;
                    plasma::Vector<3, float> color = trunkFoot * (1.0f - along) + trunkTop * along;
                    if (kind == 2 && rand() % SnowTrunkDarkPatchChanceDenominator == 0)
                        color = plasma::Vector<3, float>(50.0f, 50.0f, 50.0f);
                    color = (plasma::Vector<3, float>(50.0f, 120.0f, 60.0f) - color) *
                                colorBlend(columnX, columnY, k, zone) +
                            color;
                    if (kind == 6)
                        setBlock(columnX, columnY, k, Block(plasma::Vector<3, unsigned char>(color), BlockTree), zone);
                    else
                        setBlock(
                            columnX, columnY, k,
                            Block(plasma::Vector<3, unsigned char>(color), BlockTree | (1 << BlockFlagDestructible)),
                            zone);
                }
            }
        }
    }

    if (kind == 6) {
        // Five to eight roots, each a chain of shrinking spheres reaching out and down from the foot.
        int roots = rand() % BroadTreeRootCountRange + BroadTreeMinimumRoots;
        float start = (float)(rand() * 6.283185307179586 / double(RAND_MAX));
        double footX = toBlocks(centerX);
        // Tree shape profile: Broad-tree root reach/step/drop and sphere taper schedule.
        double footY = toBlocks(centerY);
        float reach = footRadius * 0.75f;
        for (int b = 0; b < roots; b++) {
            float angle = (float)(b * 3.141592653589793 * 2.0 / roots + start);
            plasma::Vector<3, float> direction = plasma::Vector<3, float>(cos(angle), sin(angle), 0.0f) * footRadius;
            plasma::Vector<3, float> half = direction * 0.5f;
            plasma::Vector<3, double> point(footX + half[0], footY + half[1], z + half[2]);
            float stepX = reach * cos(angle) * 0.1f;
            float stepY = sin(angle) * reach * 0.1f;
            float drop = reach * 0.05f;
            for (int s = 0; s <= 140; s += 7) {
                fillSphere((int)point[0], (int)point[1], (int)point[2], (float)(10 - s / 20),
                           Block(plasma::Vector<3, unsigned char>(trunkFoot), BlockTree | (1 << BlockFlagDestructible)),
                           zone, true, true);
                point[0] += stepX;
                point[1] += stepY;
                point[2] -= rand() * drop / float(RAND_MAX);
            }
        }
    } else {
        if (kind != 1 && kind != 3) {
            // The foot flares out over the two lowest levels; each level's spheres sit further out than the
            // one above.
            float offset = -1.0f;
            // Tree shape profile: Two-level trunk flare radius profile offsets/divisor.
            for (int k = z; k > z - 2; k--) {
                float r = (k - z + 4) * footRadius / 5.0f;
                offset += r;
                fillSphere((int)(x - offset), y, k, r,
                           Block(plasma::Vector<3, unsigned char>(trunkFoot), BlockTree | (1 << BlockFlagDestructible)),
                           zone, true, true);
                fillSphere((int)(x + 1 + offset), y, k, r,
                           Block(plasma::Vector<3, unsigned char>(trunkFoot), BlockTree | (1 << BlockFlagDestructible)),
                           zone, true, true);
                fillSphere(x, (int)(y - offset), k, r,
                           Block(plasma::Vector<3, unsigned char>(trunkFoot), BlockTree | (1 << BlockFlagDestructible)),
                           zone, true, true);
                fillSphere(x, (int)(y + 1 + offset), k, r,
                           Block(plasma::Vector<3, unsigned char>(trunkFoot), BlockTree | (1 << BlockFlagDestructible)),
                           zone, true, true);
            }
        }
        if (kind == 3) {
            // The leaning trunk's top: the leaf sprites in four turns.
            int topX = (int)(centerX + Fixed(leanX));
            int topY = (int)(centerY + Fixed(leanY));
            int crown = z + height - PalmLeafCrownDrop;
            Sprite *leafSprite = m_spriteManager.sprite(SpritePalmLeaf);
            if (leafSprite) {
                plasma::Vector<4, int> placementMargins(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                placeSprite(leafSprite,
                            plasma::Vector<3, int>(topX - leafSprite->m_size[0] / 2, topY + 1,
                                                   crown + rand() % PalmLeafCrownJitterRange),
                            0, 40, PlacementPlain, zone, false, placementMargins, 0);
                plasma::Vector<4, int> placementMargins1(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                placeSprite(leafSprite,
                            plasma::Vector<3, int>(topX + 1, topY - leafSprite->m_size[0] / 2, crown + rand() % 3), 1,
                            40, PlacementPlain, zone, false, placementMargins1, 0);
                plasma::Vector<4, int> placementMargins2(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                placeSprite(leafSprite,
                            plasma::Vector<3, int>(topX - leafSprite->m_size[0] / 2, topY - leafSprite->m_size[1],
                                                   crown + rand() % 3),
                            2, 40, PlacementPlain, zone, false, placementMargins2, 0);
                plasma::Vector<4, int> placementMargins3(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                placeSprite(leafSprite,
                            plasma::Vector<3, int>(topX - leafSprite->m_size[1], topY - leafSprite->m_size[0] / 2,
                                                   crown + rand() % 3),
                            3, 40, PlacementPlain, zone, false, placementMargins3, 0);
            }
            Sprite *innerSprite = m_spriteManager.sprite(SpritePalmLeafDiagonal);
            if (innerSprite) {
                plasma::Vector<4, int> placementMargins(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                placeSprite(innerSprite,
                            plasma::Vector<3, int>(topX + 1, topY + 1, crown + rand() % PalmInnerCrownJitterRange), 0,
                            40, PlacementPlain, zone, false, placementMargins, 0);
                plasma::Vector<4, int> placementMargins1(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                placeSprite(innerSprite,
                            plasma::Vector<3, int>(topX + 1, topY - innerSprite->m_size[1],
                                                   crown + rand() % PalmInnerCrownJitterRange),
                            1, 40, PlacementPlain, zone, false, placementMargins1, 0);
                plasma::Vector<4, int> placementMargins2(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                placeSprite(innerSprite,
                            plasma::Vector<3, int>(topX - innerSprite->m_size[1], topY - innerSprite->m_size[1],
                                                   crown + rand() % PalmInnerCrownJitterRange),
                            2, 40, PlacementPlain, zone, false, placementMargins2, 0);
                plasma::Vector<4, int> placementMargins3(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
                placeSprite(innerSprite,
                            plasma::Vector<3, int>(topX - innerSprite->m_size[1], topY + 1,
                                                   crown + rand() % PalmInnerCrownJitterRange),
                            PalmInnerCrownJitterRange, 40, PlacementPlain, zone, false, placementMargins3, 0);
            }
        }
    }
    // Now and then a plant of type 61 at the top of a tree above the snow temperature.
    if (rand() % PalmFruitChanceDenominator == 0 && temperature > 0.2f) {
        Zone::Plant plant;
        plant.type = PlantBirdAndOwlEmitter;
        plant.position = plasma::Vector<3, Fixed>(centerX, centerY, Fixed(z + height));
        zone->m_plants.push_back(plant);
    }

    // Tree shape profile: Palm frond count, vertical origin and length profile.
    if (kind == 5) {
        // A palm: fronds arching out from the upper part of the trunk, every other one only now and then,
        // each ending in foliage.
        int fronds = (int)(height / 4 + 6 + radius * 0.5f);
        // Two draws are not used.
        rand();
        rand();
        float start = (float)(rand() % 8 * 0.7853981633974483);
        for (int b = 0; b < fronds; b++) {
            if (b % 2 != 0 && rand() % PalmDiagonalFrondOmissionDenominator != 0)
                continue;
            double footX = toBlocks(centerX);
            double footY = toBlocks(centerY);
            float along = b * 0.6f / (fronds - 1) + 0.6f;
            // Tree shape profile: branch angle and crown placement coefficients.
            double footZ = height * along + z;
            float angle = (float)(b * 3.141592653589793 * 0.25 + start);
            float s = sin(angle);
            float c = cos(angle);
            plasma::Vector<3, float> direction = plasma::Vector<3, float>(c, s, 0.0f) * footRadius;
            plasma::Vector<3, double> root(direction[0] + footX, direction[1] + footY, direction[2] + footZ);
            plasma::Vector<3, float> color = trunkFoot * (1.0f - along) + trunkTop * along;
            for (int i = 0; i < 3; i++) {
                if (color[i] < 0.0f)
                    color[i] = 0.0f;
                if (color[i] > 255.0f)
                    color[i] = 255.0f;
            }
            int segments = height / 2 + rand() % (height / 2) + b * height / 4 / fronds;
            float length = radius * 8.0f * (1.0f - along) + radius;
            float lengthY = length * s;
            float lengthX = length * c;
            for (int i = 0; i <= segments * 2; i++) {
                float t = 1.0f - (float)i / (segments * 2);
                float arc = 1.0f - t * t;
                Fixed fz(i * 0.5f + root[2]);
                Fixed fy(arc * lengthY + root[1]);
                Fixed fx(arc * lengthX + root[0]);
                if (getBlock(fx, fy, fz, zone).type & (1 << BlockFlagInterior))
                    continue;
                Fixed above = fz + Fixed(1);
                if (getBlock(fx, fy, above, zone).type & (1 << BlockFlagInterior))
                    continue;
                setBlock((int)fx, (int)fy, (int)fz,
                         Block(plasma::Vector<3, unsigned char>(color), BlockTree | (1 << BlockFlagDestructible)),
                         zone);
                setBlock((int)fx, (int)fy, (int)above,
                         Block(plasma::Vector<3, unsigned char>(color), BlockTree | (1 << BlockFlagDestructible)),
                         zone);
                if (i != segments * 2 &&
                    (i % PalmFrondFoliageStep != 0 || rand() % PalmFrondFoliageChanceDenominator != 0))
                    // Tree shape profile: Palm foliage chance, radius and placement profiles. Tree shape profile:
                    // branch angle and crown placement coefficients.
                    continue;
                // Foliage, kept inside the zone.
                float blob = (rand() * (arc * 0.75f) / float(RAND_MAX) + 0.25f) * (segments * 0.5f) + 2.0f;
                // Tree shape profile: branch angle and crown placement coefficients.
                float inner = (rand() * 0.5f / float(RAND_MAX) + 0.5f) * blob;
                plasma::Vector<3, Fixed> center(fx + Fixed(blob * c * 0.25f), fy + Fixed(blob * s * 0.25f), above);
                Fixed extent(blob);
                if (center[0] - extent - Fixed(1) < Fixed(zone->m_position[0] * BlocksPerZone))
                    center[0] = Fixed(zone->m_position[0] * BlocksPerZone + blob + 1.0f);
                if (center[1] - extent - Fixed(1) < Fixed(zone->m_position[1] * BlocksPerZone))
                    center[1] = Fixed(zone->m_position[1] * BlocksPerZone + blob + 1.0f);
                if (center[0] + extent + Fixed(1) >= Fixed(zone->m_position[0] * BlocksPerZone + BlocksPerZone))
                    center[0] = Fixed(zone->m_position[0] * BlocksPerZone + BlocksPerZone - blob - 2.0f);
                if (center[1] + extent + Fixed(1) >= Fixed(zone->m_position[1] * BlocksPerZone + BlocksPerZone))
                    center[1] = Fixed(zone->m_position[1] * BlocksPerZone + BlocksPerZone - blob - 2.0f);
                growFoliage((int)center[0], (int)center[1], (int)center[2], blob, inner, leaves, leavesLight, 5, zone);
                if (emitLeaves) {
                    Zone::Plant leafEmitter;
                    leafEmitter.type = PlantLeafEmitter;
                    leafEmitter.position = center;
                    leafEmitter.color = leaves / 255.0f;
                    zone->m_plants.push_back(leafEmitter);
                }
            }
        }
    } else if (kind == 0 || kind == 4 || kind == 2) {
        // A round crown on top, and four to eight branches each ending in a smaller crown.
        int branches = rand() % RoundTreeBranchCountRange + RoundTreeMinimumBranches;
        // Tree shape profile: Round crown density, radius and branch reach profiles.
        float start = (float)(rand() * 6.283185307179586 / double(RAND_MAX));
        float density = rand() * 0.8f / float(RAND_MAX) + 0.6f;
        float crown = (float)((rand() * 1.25f / float(RAND_MAX) + 2.0) * footRadius);
        growFoliage((int)centerX, (int)centerY, z + height, crown, crown * density, leaves, leavesLight, kind, zone);
        double footY = toBlocks(centerY);
        double footX = toBlocks(centerX);
        float reach = footRadius * 0.75f;
        for (int b = 0; b < branches; b++) {
            float along = rand() * 0.5f / float(RAND_MAX) + 0.5f;
            double footZ = along * height + z;
            float angle = (float)(b * 3.141592653589793 * 2.0 / branches + start);
            float s = sin(angle);
            float c = cos(angle);
            plasma::Vector<3, float> direction = plasma::Vector<3, float>(c, s, 0.0f) * footRadius;
            plasma::Vector<3, float> half = direction * 0.5f;
            plasma::Vector<3, double> point(half[0] + footX, half[1] + footY, half[2] + footZ);
            plasma::Vector<3, float> color = trunkFoot * (1.0f - along) + trunkTop * along;
            float stepX = c * reach * 0.5f;
            float stepY = s * reach * 0.5f;
            // Tree shape profile: Round branch segment budget, thickness threshold and crown lift.
            float climb = reach * 0.5f;
            for (int step = 6; step > 0; step--) {
                if (kind != 2 && kind != 4 && footRadius > 1.5f)
                    fillSphere((int)point[0], (int)point[1], (int)point[2], 2.0f,
                               Block(plasma::Vector<3, unsigned char>(color), BlockTree | (1 << BlockFlagDestructible)),
                               zone, true, true);
                point[0] += stepX;
                point[1] += stepY;
                point[2] += rand() * climb / float(RAND_MAX);
                // Object layout: per-axis dimensions and placement offsets for this model.
            }
            float size = (float)((rand() * 1.25f / float(RAND_MAX) + 2.0) * footRadius);
            if (zone->m_position[0] * BlocksPerZone > point[0] - size - 1.0)
                point[0] = zone->m_position[0] * BlocksPerZone + size + 1.0f;
            if (zone->m_position[1] * BlocksPerZone > point[1] - size - 1.0)
                point[1] = zone->m_position[1] * BlocksPerZone + size + 1.0f;
            if (size + point[0] + 1.0 >= zone->m_position[0] * BlocksPerZone + BlocksPerZone)
                point[0] = zone->m_position[0] * BlocksPerZone + BlocksPerZone - size - 2.0f;
            if (size + point[1] + 1.0 >= zone->m_position[1] * BlocksPerZone + BlocksPerZone)
                point[1] = zone->m_position[1] * BlocksPerZone + BlocksPerZone - size - 2.0f;
            if (b > 4)
                // Tree shape profile: branch angle and crown placement coefficients.
                point[2] += density * size * 0.5f;
            float lift = density * size * 0.25f;
            growFoliage((int)point[0], (int)point[1], (int)(lift + point[2]), size, density * size, leaves, leavesLight,
                        kind, zone);
            if (emitLeaves) {
                Zone::Plant leafEmitter;
                leafEmitter.type = PlantLeafEmitter;
                leafEmitter.position = plasma::Vector<3, Fixed>(
                    plasma::Vector<3, double>(point[0] + 0.0, point[1] + 0.0, point[2] + lift));
                leafEmitter.color = leaves / 255.0f;
                zone->m_plants.push_back(leafEmitter);
            }
        }
    } else if (kind == 6) {
        // A broad tree: eight to twelve long branches climbing from the trunk, thinning as they go, with
        // foliage along their outer part.
        int branches = rand() % BroadTreeBranchCountRange + BroadTreeMinimumBranches;
        float start = (float)(rand() * 6.283185307179586 / double(RAND_MAX));
        // Two draws are not used.
        rand();
        rand();
        double footY = toBlocks(centerY);
        double footX = toBlocks(centerX);
        float reach = footRadius * 0.75f;
        for (int b = 0; b < branches; b++) {
            float along = rand() * 0.5f / float(RAND_MAX) + 0.5f;
            double footZ = height * along + z;
            float angle = (float)(b * 3.141592653589793 * 2.0 / branches + start);
            float s = sin(angle);
            plasma::Vector<3, float> direction(cos(angle), s, 0.0f);
            float side = (float)(angle + 1.5707963267948966);
            float sideY = sin(side);
            float sideX = cos(side);
            plasma::Vector<3, float> half = direction * footRadius * 0.5f;
            plasma::Vector<3, double> point(half[0] + footX, half[1] + footY, half[2] + footZ);
            // Tree shape profile: Broad branches: step/taper schedule and outer foliage density/placement.
            plasma::Vector<3, float> color = trunkFoot * (1.0f - along) + trunkTop * along;
            for (int step = 0, length = 0; step <= 200; step += 5, length++) {
                float thickness = 6.5f - step / 40.0f;
                fillSphere((int)point[0], (int)point[1], (int)point[2], thickness,
                           Block(plasma::Vector<3, unsigned char>(color), BlockTree | (1 << BlockFlagDestructible)),
                           zone, true, true);
                if (length > 10) {
                    // Foliage on both sides of the branch, more of it further out.
                    for (int tries = 5; tries > 0; tries--) {
                        // The original draws inner, radius, z, y, x in that order for both calls.
                        if (rand() % 120 < length - 10)
                            growFoliage((int)(rand() % 20 + point[0] - 10.0 + sideX * 4.0f),
                                        (int)(rand() % 20 + point[1] - 10.0 + sideY * 4.0f),
                                        (int)(rand() % (2 - (int)(thickness * -2.0f)) + (point[2] - 3.0)),
                                        (float)(rand() % 4 + 4), (float)(rand() % 4 + 4), leaves, leavesLight, 6, zone);
                        // The other side's foliage rises twice as high: (1 - t) * 2.
                        if (rand() % 120 < length - 10)
                            growFoliage((int)(rand() % 20 + point[0] - 10.0 - sideX * 4.0f),
                                        (int)(rand() % 20 + point[1] - 10.0 - sideY * 4.0f),
                                        (int)(rand() % ((1 - (int)(thickness * -2.0f)) * 2) + (point[2] - 3.0)),
                                        (float)(rand() % 4 + 4), (float)(rand() % 4 + 4), leaves, leavesLight, 6, zone);
                    }
                    if (emitLeaves) {
                        Zone::Plant leafEmitter;
                        leafEmitter.type = PlantLeafEmitter;
                        leafEmitter.position = plasma::Vector<3, Fixed>(point);
                        leafEmitter.color = leaves / 255.0f;
                        zone->m_plants.push_back(leafEmitter);
                    }
                    // Tree shape profile: Broad branch lateral and vertical growth rates.
                }
                point[0] += direction[0] * reach * 0.075f;
                point[1] += direction[1] * reach * 0.075f;
                point[2] += reach * 0.1f * length / 40.0f;
            }
        }
    } else if (kind == 1) {
        // A conifer: thirty layers of discs spiralling up the trunk, each layer a slanting line of discs
        // from the rim inwards, shaded from the needle colour at the trunk to the tip colour at the rim,
        // snowy in cold columns; two needle blocks on top.
        float angle = (float)(rand() * 6.283185307179586 / double(RAND_MAX));
        // The original draws b, g, r in that order for both colours.
        plasma::Vector<3, float> needles(
            plasma::Vector<3, unsigned char>(rand() % 100, rand() % 50 + 100, rand() % 100));
        plasma::Vector<3, float> tips(plasma::Vector<3, unsigned char>(rand() % 100, rand() % 50 + 100, rand() % 100));
        // Tree shape profile: Conifer droop, disc sampling, layer angle step and crown height fraction.
        float spread = (float)radius;
        float droop = -radius * 0.3f;
        int segments = height / 3;
        for (int layer = 0; layer < 30; layer++) {
            angle = (float)(angle + 0.9424778335276408);
            float along = layer * 0.7f / 30.0f + 0.3f;
            float c = cos(angle) * spread;
            float s = sin(angle) * spread;
            float out = (1.0f - along) * 2.0f;
            double layerZ = along * height + z;
            double rimX = x + (double)(out * c);
            double rimY = y + (double)(out * s);
            double rimZ = layerZ + (double)(out * droop);
            // Each disc lies on the line from the rim to the trunk, interpolated in double precision.
            for (int i = segments; i > 0; i--) {
                float t = (float)i / segments;
                float u = 1.0f - t;
                double px = t * rimX + u * (double)x;
                double py = t * rimY + u * (double)y;
                double pz = t * rimZ + u * layerZ;
                if (temperature < 0.3f)
                    fillCircle((int)px, (int)py, (int)(pz + 1.0), 2.0f,
                               Block(plasma::Vector<3, unsigned char>(snowColor(x, y)),
                                     BlockLeaves | (1 << BlockFlagDestructible)),
                               zone, true, false);
                plasma::Vector<3, float> color = needles * u + tips * t;
                fillCircle((int)px, (int)py, (int)pz, 2.0f,
                           Block(plasma::Vector<3, unsigned char>(color), BlockLeaves | (1 << BlockFlagDestructible)),
                           zone, true, false);
            }
            setBlock(x, y, z + height + 1,
                     Block(plasma::Vector<3, unsigned char>(needles), BlockLeaves | (1 << BlockFlagDestructible)),
                     zone);
            setBlock(x, y, z + height,
                     Block(plasma::Vector<3, unsigned char>(needles), BlockLeaves | (1 << BlockFlagDestructible)),
                     zone);
        }
    }
}

namespace {
enum {
    LedgeMushroomChanceDenominator = 4
};
enum {
    RiverTerraceStepBlocks = 5
};
enum {
    RiverbankReedChanceDenominator = 200
};
enum {
    DungeonPlacementInset = 32
};
enum {
    ReedMoundRadiusRange = 3
};
enum {
    TreeGridRows = 8
};
enum {
    TreeGridSampleCenterOffset = 8
};
enum {
    TreeHumiditySampleOffset = 8
};
enum {
    TreeGridRowInset = 8
};
enum {
    SignpostZoneMargin = 16
};
enum {
    SignpostZoneRandomRange = 224
};
enum {
    WildCreatureGridWidth = 3
};
enum {
    WildCreatureGridHeight = 3
};
enum {
    ZoneSeedRowShift = 16
};
enum {
    LedgeSpawnChanceDenominator = 30
};
enum {
    LedgeSpawnKindChoiceCount = 4
};
enum {
    OreChoiceBucketCount = 10
};
enum {
    GemChoiceBucketCount = 100
};
enum {
    CanyonRockLatticeExtent = 0x300
};
enum {
    CanyonRockLatticeStep = 0x100
};
enum {
    CanyonRockChanceDenominator = 4
};
enum {
    CanyonRockLatticeDivisor = 3
};
enum {
    CanyonRockLatticeInset = 42
};
enum {
    LandmarkRockHeightRange = 100
};
enum {
    LandmarkTreeHeight = 80
};
enum {
    BoulderCountRange = 4
};
enum {
    BoulderRadiusRange = 40
};
enum {
    BoulderMinimumRadius = 10
};
enum {
    BoulderHeightRange = 25
};
enum {
    CampPlacementAttempts = 10
};
enum {
    CampPlacementMargin = 48
};
enum {
    CampPlacementRandomRange = 160
};
enum {
    MoundCountRange = 10
};
enum {
    MoundRadiusRange = 8
};
enum {
    MoundMinimumRadius = 3
};
enum {
    PortalPillarCountRange = 3
};
enum {
    PortalMinimumPillars = 6
};
enum {
    EncounterLatticeExtent = 0xfc
};
enum {
    EncounterLatticeStep = 18
};
enum {
    EncounterZoneInset = 4
};
enum {
    WildGroupGridInset = 24
};
enum {
    WildGroupGridStep = 85
};
enum {
    WildGroupGridJitterRange = 10
};
enum {
    WildGroupColdRejectionDenominator = 4
};
enum {
    HarvestablePlantChanceDenominator = 3
};
enum {
    HarvestablePlantKindChoiceCount = 4
};
enum {
    RoadLampLatticeYPhase = 90
};
enum {
    RoadLampLatticeModulus = 470
};
enum {
    RoadLampChanceDenominator = 16
};
enum {
    UnderwaterDecorationDepthLimit = 4
};
enum {
    LedgeSpawnRowMultiplier = 3
};
enum {
    LedgeSpawnGridModulus = 7
};
enum {
    InitialGroundDepth = 8
};
enum {
    CanyonRockHorizontalSizeRange = 10
};
enum {
    CanyonRockMinimumHorizontalSize = 20
};
enum {
    CanyonRockVerticalSizeRange = 16
};
enum {
    PortalPillarBaseLift = 4
};
enum {
    PortalPillarVerticalSampleExtent = 20
};
enum {
    EncounterLatticeColumnStep = 3
};
enum {
    EncounterLatticeSelectionModulus = 5
};
enum {
    LandmarkRockMinimumHeight = 100
};
enum {
    LandmarkTreeRadius = 80
};
}

// Generates the zone at zone coordinates (256 x 256 block columns): the regions around it first,
// then the terrain columns, the region record's special structures, plants, objects, spawns and
// the saved state.
void World::generateZone(int x, int y)
{
    if (x < 0 || y < 0 || x >= ZonesPerWorld || y >= ZonesPerWorld)
        return;
    int regionX = x / ZonesPerRegion;
    int regionY = y / ZonesPerRegion;
    for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++)
            generateRegion(regionX + i, regionY + j);
    Region *region = m_regions[regionX][regionY];
    if (this->zone(x, y))
        return;
    Zone *zone = new Zone;
    srand(m_generationSeed + (y << ZoneSeedRowShift) + x);
    zone->m_position[0] = x;
    zone->m_position[1] = y;
    int baseY = y * 256;
    int baseX = x * 256;
    // The zone's centre column; the two lookups' results are not used.
    plasma::Vector<2, int> center(baseX + (BlocksPerZone / 2), baseY + (BlocksPerZone / 2));
    nearestRegionRecord(center[0], center[1]);
    jitter(center[0], center[1]);
    int minimumZ = 0;
    int maximumZ = 0;
    int grassColumnCount = 0;
    int vegetationCount = 0;
    Region::Record &record = region->m_records[(x / 8 % 8) * 8 + y / 8 % 8];
    std::list<plasma::Vector<3, int>> plants;

    // The terrain parameters of each column.
    for (int bx = baseX; bx < baseX + 256; bx++) {
        for (int by = baseY; by < baseY + 256; by++) {
            Field &field = zone->m_fields[(by % 256) * 256 + bx % 256];
            field.humidity = blendedHumidity(bx, by);
            field.temperature = blendedTemperature(bx, by);
            field.firelands = blendedFirelands(bx, by);
        }
    }
    // Terrain heights of the 257 x 257 column corners.
    float *heights = new float[257 * 257];
    for (int bx = baseX; bx <= baseX + 256; bx++)
        for (int by = baseY; by <= baseY + 256; by++)
            heights[(by - baseY) * 257 + bx - baseX] = terrainHeight(bx, by, zone);

    bool unused1330 = true; // never read
    for (int bx = baseX; bx < baseX + 256; bx++) {
        for (int by = baseY; by < baseY + 256; by++) {
            Field *field = &zone->m_fields[(by % 256) * 256 + bx % 256];
            float height = heights[(by - baseY) * 257 + bx - baseX];
            bool steep = fabs(height - heights[(by - baseY) * 257 + bx - baseX + 1]) > 0.3f ||
                         fabs(height - heights[(by - baseY) * 257 + bx - baseX + 257]) > 0.3f;
            float moisture = vegetationDensity(bx, by, zone);
            if (moisture > 0.25f)
                // Column ridge noise frequencies and channel offsets.
                vegetationCount++;
            float ridge1 = (float)(1.0 - fabs(noise(bx * 0.04 + 432.0, by * 0.04 + 432.0) +
                                              noise(bx * 0.08 + 432.0, by * 0.08 + 432.0) * 0.05));
            float ridges = 0.0f;
            ridges += (1.0f - ridge1 * ridge1 * ridge1) * (1.0f - ridge1 * ridge1 * ridge1);
            float ridge2 = (float)(1.0 - fabs(noise(bx * 0.08 + 4234.0, by * 0.08 + 234.0) * 0.05 +
                                              noise(bx * 0.05 + 423432.0, by * 0.05 + 54352.0)));
            float roughness = (1.0f - ridge2 * ridge2 * ridge2) * (1.0f - ridge2 * ridge2 * ridge2) + ridges;
            float grass = moisture;
            if (record.kind == Region::Record::SceneryCanyon || record.kind == Region::Record::SceneryPeak) {
                float distance = 1.0f - record.shapeDistance(Fixed(bx), Fixed(by));
                if (distance <= 0.0f)
                    distance = 0.0f;
                else
                    distance *= distance;
                distance *= 2.0f;
                if (distance > 1.0f)
                    distance = 1.0f;
                // Canyon/peak grass blend noise parameters.
                float edge = 1.0f - (1.0f - distance * distance) * (1.0f - distance * distance);
                float blend = noise(bx * 0.01 + 985.0, by * 0.01 + 98584.0) * 1.3f + 1.0f;
                if (blend < 0.0f)
                    blend = 0.0f;
                else if (blend > 1.0f)
                    blend = 1.0f;
                grass = smoothStep(blend) * edge * 0.4f + moisture;
            }
            // Relief flattening scale and column elevation noise profile.
            float relief = grass * roughness;
            float flat = relief * 1.25f;
            if (flat < 0.0f)
                flat = 0.0f;
            else if (flat > 1.0f)
                flat = 1.0f;
            float sand = smoothStep(flat);
            float level = (float)((noise(bx * 0.01 + 34432.0, by * 0.01 + 8992.0) + 1.5) * 60.0) * moisture +
                          relief * 8.0f + height;
            int z = (int)level;
            Block block = terrainBlock(bx, by, z, field->humidity, field->temperature, zone);
            if (sand > 0.5f)
                block.type = BlockBuilding;
            block.color = plasma::Vector<3, unsigned char>(rockColor(bx, by, z, zone) * sand +
                                                           plasma::Vector<3, float>(block.color) * (1.0f - sand));
            field->m_baseZ = z;
            if ((float)minimumZ > level)
                minimumZ = z;
            if (level > (float)maximumZ)
                maximumZ = z;
            grass *= 8.0f;
            if (grass > 1.0f)
                grass = 1.0f;
            if (steep) {
                field->setBlock(0, block);
            } else {
                plasma::Vector<3, float> color = groundColor(bx, by, z, zone) * (1.0f - grass);
                field->setBlock(
                    0, Block(plasma::Vector<3, unsigned char>(
                                 (plasma::Vector<3, float>(block.color) + rockColor(bx, by, z, zone)) * (grass * 0.5f) +
                                 color),
                             BlockBuilding));
            }
            field->setBlock(1, block);
            if ((block.type & BlockTypeMask) == BlockGrass)
                grassColumnCount++;
            // Cliff base noise, ridges, slope transition, rise and shelf tuning.
            field->m_groundZ = field->m_baseZ - InitialGroundDepth;
            // Rock rises above the terrain where the cliff noise allows it.
            float cliffBase = (noise(bx * 0.01 + 435.0, by * 0.01 + 847.0) + 1.0f) * 20.0f + height;
            if (level > cliffBase) {
                float ridge =
                    1.0f - (float)fabs(noise(bx * 0.005 + m_cliffNoiseOffsets[0], by * 0.005 + m_cliffNoiseOffsets[1]) +
                                       noise(m_cliffNoiseOffsets[0] + bx * 0.01, m_cliffNoiseOffsets[1] + by * 0.01)) *
                               4.0f;
                if (ridge >= 0.0f) {
                    ridge *= ridge;
                    float rise = 1.0f - (1.0f - ridge) * (1.0f - ridge);
                    float rest = 1.0f - ridge;
                    float overhang = 1.0f - rest * rest * rest * rest;
                    float steepness =
                        (noise(bx * 0.03 + 7635.0, by * 0.03 + 123847.0) * 10.0f + (level - cliffBase - 20.0f)) / 10.0f;
                    if (steepness < 0.0f)
                        steepness = 0.0f;
                    else if (steepness > 1.0f)
                        steepness = 1.0f;
                    float cover = 1.0f - (1.0f - steepness * steepness) * (1.0f - steepness * steepness);
                    float fall = 1.0f - steepness;
                    float fall7 = fall * fall * fall * fall * fall * fall * fall;
                    float top =
                        ((moisture * 20.0f + 4.0f) * rise + cliffBase) * (1.0f - fall7) + (level + 2.0f) * fall7;
                    float shelf = roughness * moisture * 6.0f + cliffBase;
                    float bottom = (1.0f - overhang) * level + overhang * shelf + 2.0f;
                    if (level - 2.0f > shelf) {
                        int z = (int)level;
                        for (int i = 0; i < 2; i++, z++) {
                            Block below = getBlock(bx, by, z, zone);
                            plasma::Vector<3, float> color = rockColor(bx, by, (int)(i + level), zone);
                            below.color = plasma::Vector<3, unsigned char>(
                                plasma::Vector<3, float>(below.color) * cover + color * (1.0f - cover));
                            setBlock(bx, by, z, below, zone);
                        }
                    }
                    if (bottom < top) {
                        Block air(plasma::Vector<3, unsigned char>(0, 0, 0),
                                  level > top ? (1 << BlockFlagInterior) : 0);
                        int first = (int)bottom;
                        for (int z = first; (float)z < top; z++)
                            setBlock(bx, by, z,
                                     z > 0 ? air : Block(plasma::Vector<3, unsigned char>(0, 0, 0), BlockWater), zone);
                        int z = (int)(bottom - 1.0f);
                        Block rock = terrainBlock(bx, by, z, field->humidity, field->temperature, zone);
                        if (sand > 0.5f || cover > 0.5f)
                            rock.type = BlockBuilding;
                        plasma::Vector<3, float> color = plasma::Vector<3, float>(rock.color) * (1.0f - cover);
                        plasma::Vector<3, float> placementMargins = rockColor(bx, by, z, zone);
                        rock.color = plasma::Vector<3, unsigned char>(
                            (groundColor(bx, by, z, zone) + placementMargins) * (cover * 0.4f) + color);
                        color = plasma::Vector<3, float>(rock.color) * (1.0f - sand);
                        rock.color = plasma::Vector<3, unsigned char>(rockColor(bx, by, z, zone) * sand + color);
                        setBlock(bx, by, z, rock, zone);
                        // Now and then something lives on a high ledge.
                        if (top - bottom > 2.0f && (bx + by * LedgeSpawnRowMultiplier) % LedgeSpawnGridModulus == 0) {
                            if (rand() % LedgeSpawnChanceDenominator == 0) {
                                switch (rand() % LedgeSpawnKindChoiceCount) {
                                case 0:
                                    if (top <= level && rand() % LedgeMushroomChanceDenominator == 0) {
                                        Zone::ZoneItem item;
                                        item.item.category = ItemCategoryMaterial;
                                        item.item.id = MaterialItemShimmerMushroom;
                                        item.item.material = 9;
                                        item.item.level = 1;
                                        item.position = plasma::Vector<3, Fixed>(Fixed(bx) - Fixed(-0.5),
                                                                                 Fixed(by) - Fixed(-0.5), Fixed(first));
                                        item.rotation = (float)(rand() * 6.283185307179586 / double(RAND_MAX));
                                        item.scale = 0.1f;
                                        zone->m_items.push_back(item);
                                    }
                                    break;
                                case 1:
                                    if (top <= level) {
                                        Spawn *spawn = new Spawn;
                                        spawn->position = plasma::Vector<3, Fixed>(
                                            Fixed(bx) - Fixed(-0.5), Fixed(by) - Fixed(-0.5), Fixed(first));
                                        spawn->level = 1;
                                        spawn->hostilityType = HostilityAttackableObject;
                                        spawn->multipliers.hp = 25.0f;
                                        switch (rand() % OreChoiceBucketCount) {
                                        case 0:
                                            spawn->race = RaceGoldDeposit;
                                            break;
                                        case 1:
                                            spawn->race = RaceSilverDeposit;
                                            break;
                                        case 3:
                                            switch (rand() % GemChoiceBucketCount) {
                                            case 0:
                                                spawn->race = RaceDiamondDeposit;
                                                break;
                                            case 1:
                                            case 2:
                                            case 3:
                                                spawn->race = RaceRubyDeposit;
                                                break;
                                            case 4:
                                            case 5:
                                            case 6:
                                            case 7:
                                            case 8:
                                                spawn->race = RaceSapphireDeposit;
                                                break;
                                            default:
                                                spawn->race = RaceEmeraldDeposit;
                                                break;
                                            }
                                            break;
                                        default:
                                            spawn->race = RaceIronDeposit;
                                            break;
                                        }
                                        zone->m_spawns.push_back(spawn);
                                    }
                                    break;
                                case 2:
                                    if (top <= level) {
                                        Spawn *spawn = new Spawn;
                                        spawn->position = plasma::Vector<3, Fixed>(
                                            Fixed(bx) - Fixed(-0.5), Fixed(by) - Fixed(-0.5), Fixed(first));
                                        spawn->level = levelAt(Fixed(bx), Fixed(by));
                                        spawn->hostilityType = HostilityEnemy;
                                        spawn->race = RaceBat;
                                        zone->m_spawns.push_back(spawn);
                                    }
                                    break;
                                case 3:
                                    plants.push_back(plasma::Vector<3, int>(bx, by, (int)(bottom + 2.0f)));
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            // Region structures that cover this column.
            if (record.kind == Region::Record::SceneryLake) {
                if (record.shapeDistance(Fixed(bx), Fixed(by)) < 0.25f) {
                    for (int z = (int)(record.height - 25.0f); z >= field->m_baseZ; z--) {
                        if (getBlock(bx, by, z, zone).isSolid()) {
                            setBlock(bx, by, z,
                                     Block(plasma::Vector<3, unsigned char>(rockColor(bx, by, z, zone)), BlockWet),
                                     zone);
                        } else {
                            setBlock(bx, by, z, waterCube, zone);
                        }
                    }
                }
            }
        }
    }
    delete[] heights;

    int cellIndex = (x % ZonesPerRegion) * ZonesPerRegion + y % ZonesPerRegion;
    zone->cell = region->m_cells[cellIndex];
    std::list<plasma::Vector<3, Fixed>> structures;
    terrainHumidity(baseX + (BlocksPerZone / 2), baseY + (BlocksPerZone / 2), zone);
    float temperature = terrainTemperature(baseX + (BlocksPerZone / 2), baseY + (BlocksPerZone / 2), zone);
    float humidity = terrainHumidity(baseX + (BlocksPerZone / 2), baseY + (BlocksPerZone / 2), zone);
    if (record.kind == Region::Record::SceneryCanyon) {
        // Rocks around the record's centre.
        for (int i = 0; i < CanyonRockLatticeExtent; i += CanyonRockLatticeStep) {
            for (int j = 0; j < 0x300; j += 0x100) {
                if (rand() % CanyonRockChanceDenominator == 0)
                    continue;
                int px = zone->m_position[0] * 256 + i / CanyonRockLatticeDivisor + CanyonRockLatticeInset;
                int py = zone->m_position[1] * 256 + j / CanyonRockLatticeDivisor + CanyonRockLatticeInset;
                float inside = 1.0f - record.shapeDistance(Fixed(px), Fixed(py));
                if (inside <= 0.0f || inside * inside < 0.5f)
                    continue;
                bool crowded = false;
                for (std::list<plasma::Vector<3, Fixed>>::iterator it = structures.begin(); it != structures.end();
                     ++it) {
                    float dx = (float)(Fixed(px) - (*it)[0]);
                    float dy = (float)(Fixed(py) - (*it)[1]);
                    if (dx * dx + dy * dy < 6400.0f) {
                        crowded = true;
                        break;
                    }
                }
                if (crowded)
                    continue;
                int sizeX = rand() % CanyonRockHorizontalSizeRange + CanyonRockMinimumHorizontalSize;
                int sizeY = rand() % 10 + 20;
                int sizeZ = rand() % CanyonRockVerticalSizeRange + 20;
                float sx = px - m_spawnPoint[0];
                float sy = py - m_spawnPoint[1];
                if (sx * sx + sy * sy < 3600.0f)
                    continue;
                Field *ground = getField(px, py, zone);
                buildRock(px, py, ground->m_baseZ + ground->m_size, sizeX, sizeY, sizeZ, zone);
            }
        }
    }
    if (record.kind == Region::Record::SceneryPeak || record.kind == Region::Record::SceneryLake) {
        int cx = (int)record.center[0];
        int cy = (int)record.center[1];
        if (plasma::Vector<2, int>(cx / 256, cy / 256) == zone->m_position) {
            Field *ground = getField(cx, cy, zone);
            int z = ground->m_baseZ + ground->m_size;
            while (!getBlock(cx, cy, z, zone).isSolid())
                z--;
        }
    }
    int recordType = record.kind;
    bool placed = false;
    if (recordType == Region::Record::SceneryRock) {
        int cx = (int)record.center[0];
        int cy = (int)record.center[1];
        if (plasma::Vector<2, int>(cx / 256, cy / 256) == zone->m_position) {
            int px = zone->m_position[0] * 256 + (BlocksPerZone / 2);
            int py = zone->m_position[1] * 256 + (BlocksPerZone / 2);
            int height = rand() % LandmarkRockHeightRange + LandmarkRockMinimumHeight;
            Field *ground = getField(px, py, zone);
            buildRock(px, py, ground->m_baseZ + ground->m_size, 100, 100, height, zone);
            placed = true;
        }
    } else if (recordType == Region::Record::SceneryTree) {
        int cx = (int)record.center[0];
        int cy = (int)record.center[1];
        if (plasma::Vector<2, int>(cx / 256, cy / 256) == zone->m_position) {
            int px = zone->m_position[0] * 256 + (BlocksPerZone / 2);
            int py = zone->m_position[1] * 256 + (BlocksPerZone / 2);
            rand();
            Field *ground = getField(px, py, zone);
            buildTree(px, py, ground->m_baseZ + ground->m_size, LandmarkTreeRadius, LandmarkTreeHeight, 6, zone);
            int z = ground->m_baseZ + ground->m_size;
            while (!getBlock(px, py, z, zone).isSolid())
                z--;
            placed = true;
        }
    }
    if (!placed) {
        int cellType = region->m_cells[cellIndex].kind;
        if (cellType != Region::Cell::StructurePortal && cellType != Region::Cell::StructureQuarter &&
            cellType != Region::Cell::StructureDungeon) {
            // Boulders, more of them outside the mild temperature range. Boulder density climate curve:
            // temperature and humidity bands add a second count draw.
            int count = rand() % BoulderCountRange;
            if (temperature > 0.6f || temperature < 0.3f) {
                if (humidity > 0.7f || (humidity < 0.4f && (humidity > 0.2f || temperature < 0.8f)))
                    count += 2;
            }
            for (; count > 0; count--) {
                int sizeX = rand() % BoulderRadiusRange + BoulderMinimumRadius;
                int sizeY = rand() % 40 + 10;
                // Mound/boulder placement layout: radius-dependent bounds keep the generated ellipsoid inside the
                // Zone.
                int sizeZ = rand() % BoulderHeightRange + 10;
                int px = rand() % ((64 - sizeX) * 4) + (zone->m_position[0] * 128 + sizeX) * 2;
                int py = rand() % ((64 - sizeY) * 4) + (zone->m_position[1] * 128 + sizeY) * 2;
                float sx = px - m_spawnPoint[0];
                float sy = py - m_spawnPoint[1];
                if (sx * sx + sy * sy < 3600.0f)
                    continue;
                bool crowded = false;
                for (std::list<plasma::Vector<3, Fixed>>::iterator it = structures.begin(); it != structures.end();
                     ++it) {
                    float dx = (float)(Fixed(px) - (*it)[0]);
                    float dy = (float)(Fixed(py) - (*it)[1]);
                    if (dx * dx + dy * dy < 6400.0f) {
                        crowded = true;
                        break;
                    }
                }
                if (crowded)
                    // Boulder placement gate curve: scenery influence, river proximity and vegetation density.
                    continue;
                if (recordInfluence(px, py) > 0.6f)
                    continue;
                if (1.0f - riverDistance(px, py, zone) * 50.0f >= 0.0f)
                    continue;
                float moisture = vegetationDensity(px, py, zone);
                Field *ground = getField(px, py, zone);
                int top = ground->m_baseZ + ground->m_size;
                if (rand() % 2 != 0 && moisture < 0.25f)
                    buildArch(px, py, top, sizeX, sizeY, sizeZ / 2, zone);
                else
                    buildRock(px, py, top, sizeX, sizeY, sizeZ, zone);
            }
        }
    }

    // Paved paths: stone along the path, cleared above it.
    for (int bx = baseX; bx < baseX + 256; bx++) {
        for (int by = baseY; by < baseY + 256; by++) {
            float edge = 1.0f - roadDistance(bx, by, zone) * 50.0f;
            float influence = recordInfluence(bx, by);
            if (edge < 0.0f)
                continue;
            float ground = terrainHeight(bx, by, zone);
            if (ground < 0.0f)
                ground = 0.0f;
            int z = (int)(ground + 1.0f);
            float rest = 1.0f - edge;
            // Noise field coefficients: coordinate frequency, independent channel offsets and output
            // amplitude.
            float lift = 1.0f - rest * rest * rest;
            // The lift is added to the ground before the noise.
            int top = (int)((noise(bx * 0.02 + 55432.0, by * 0.02 + 974.0) + 1.0f) * 4.0f + (lift * 5.0f + ground));
            if (!getBlock(bx, by, z, zone).isSolid() || (getBlock(bx, by, z, zone).type & (1 << BlockFlagInterior)))
                // Road stone palette and patch noise parameters.
                continue;
            // Paved-road stone starts from neutral gray RGB (140, 140, 140).
            plasma::Vector<3, float> stone =
                plasma::Vector<3, float>(40.0f, 40.0f, 40.0f) * noise(bx * 0.05 + 843.0, by * 0.05 + 984.0) +
                plasma::Vector<3, float>(140.0f, 140.0f, 140.0f);
            if (noise(bx / 2 * 234 + 4834, by / 2 * 234 + 49434) > 0.5f)
                stone += plasma::Vector<3, float>(20.0f, 20.0f, 20.0f) * noise(bx / 2 * 234, by / 2 * 234 + 49);
            if (region->m_style == 1 || region->m_style == 4 || region->m_style == 5)
                stone = groundColor(bx, by, z, zone);
            plasma::Vector<3, float> color = stone * influence + groundColor(bx, by, z, zone) * (1.0f - influence);
            for (int i = 0; i < 3; i++) {
                if (color[i] < 0.0f)
                    color[i] = 0.0f;
                if (color[i] > 255.0f)
                    color[i] = 255.0f;
            }
            // Road width scale, headroom noise and centre clearing threshold.
            setBlock(bx, by, z - 1, Block(plasma::Vector<3, unsigned char>(color), BlockRoad), zone);
            if (influence > 0.92f) {
                Field *column = getField(bx, by, zone);
                top = column->m_baseZ + column->m_size;
            }
            for (; z < top; z++) {
                if (!(getBlock(bx, by, z, zone).type & (1 << BlockFlagInterior)))
                    setBlock(bx, by, z, airCube, zone);
            }
        }
    }

    // Rivers: water in steps of five blocks, banks of wet earth.
    std::list<plasma::Vector<3, int>> reeds;
    for (int bx = baseX; bx < baseX + 256; bx++) {
        for (int by = baseY; by < baseY + 256; by++) {
            float edge = 1.0f - riverDistance(bx, by, zone) * 50.0f;
            if (edge < 0.0f)
                continue;
            if (recordInfluence(bx, by) > 0.95f)
                continue;
            float ground = terrainHeight(bx, by, zone);
            if (ground < 0.0f)
                ground = 0.0f;
            int surface = (int)ground / RiverTerraceStepBlocks * RiverTerraceStepBlocks;
            float depth = (ground - surface) / 5.0f;
            if (depth < 0.5f) {
                depth *= 2.0f;
            } else {
                depth = 1.0f - (depth - 0.5f) * 4.0f;
                if (depth < 0.0f)
                    depth = (depth + 1.0f) * (depth + 1.0f) - 1.0f;
            }
            int bed = surface;
            if (depth < 0.0f)
                bed = (int)(surface - depth * 5.0f);
            for (int z = (int)(surface - depth * 5.0f + 2.0f); z <= surface; z++) {
                if (!(getBlock(bx, by, z, zone).type & (1 << BlockFlagInterior))) {
                    // River water blue endpoint palette.
                    setBlock(bx, by, z,
                             Block(plasma::Vector<3, unsigned char>(plasma::Vector<3, float>(0.0f, 0.0f, 0.0f) * depth +
                                                                    plasma::Vector<3, float>(0.0f, 0.0f, 255.0f) *
                                                                        (1.0f - depth)),
                                   BlockWater),
                             zone);
                }
            }
            if (bed >= surface && (getBlock(bx, by, bed, zone).type & BlockTypeMask) != BlockWater &&
                !(getBlock(bx, by, bed, zone).type & (1 << BlockFlagInterior))) {
                setBlock(bx, by, bed, Block(plasma::Vector<3, unsigned char>(rockColor(bx, by, bed, zone)), BlockWet),
                         zone);
                if (rand() % RiverbankReedChanceDenominator == 0)
                    reeds.push_back(plasma::Vector<3, int>(bx, by, bed));
            }
            // River width, scenery suppression, terraced bed profile and headroom noise.
            float rest = 1.0f - edge;
            // As for the paths, the lift is added to the ground first.
            int top = (int)((noise(bx * 0.02 + 55432.0, by * 0.02 + 974.0) + 1.0f) * 2.0f +
                            ((1.0f - rest * rest * rest) * 5.0f + ground));
            for (int z = bed + 1; z < top; z++) {
                if (!(getBlock(bx, by, z, zone).type & (1 << BlockFlagInterior)))
                    setBlock(bx, by, z, airCube, zone);
            }
        }
    }

    // The cell's dungeon.
    if (region->m_cells[cellIndex].kind == Region::Cell::StructureDungeon)
        generateDungeon(zone, region, baseX + DungeonPlacementInset, baseY + DungeonPlacementInset,
                        region->m_cells[cellIndex].style, false);
    if (region->m_cells[cellIndex].kind == Region::Cell::StructurePalace)
        generateDungeon(zone, region, baseX + 32, baseY + 32, Region::Record::DungeonPalace, true);

    // Lumps of earth around the river reeds and the ledge plants.
    for (std::list<plasma::Vector<3, int>>::iterator it = reeds.begin(); it != reeds.end(); ++it) {
        int cx = (*it)[0];
        int cy = (*it)[1];
        int cz = (*it)[2];
        int radiusX = rand() % ReedMoundRadiusRange + 2;
        int radiusY = rand() % ReedMoundRadiusRange + 2;
        int radiusZ = rand() % ReedMoundRadiusRange + 2;
        for (int i = cx - radiusX; i <= cx + radiusX; i++) {
            // Noise field coefficients: coordinate frequency, independent channel offsets and output
            // amplitude.
            for (int j = cy - radiusY; j <= cy + radiusY; j++) {
                float wobble = noise(i * 0.05, j * 0.05) * 0.8f;
                float dx = (float)(i - cx) / radiusX;
                float dy = (float)(j - cy) / radiusY;
                for (int k = cz + radiusZ * 2; k >= cz - radiusZ * 2; k--) {
                    float dz = (float)(k - cz) / radiusZ + wobble;
                    if (dz * dz + (dx * dx + dy * dy) > 1.0f)
                        continue;
                    const Block &block = getBlock(i, j, k, zone);
                    if (block.isAboveWater() && !(block.type & (1 << BlockFlagInterior)))
                        setBlock(i, j, k,
                                 Block(plasma::Vector<3, unsigned char>(rockColor(i, j, k, zone)), BlockBuilding),
                                 zone);
                }
            }
        }
    }
    for (std::list<plasma::Vector<3, int>>::iterator it = plants.begin(); it != plants.end(); ++it) {
        int cx = (*it)[0];
        int cy = (*it)[1];
        // Reed/ledge mound radii and outline noise parameters.
        int cz = (*it)[2];
        int radiusX = rand() % 4 + 4;
        int radiusY = rand() % 4 + 4;
        int radiusZ = rand() % 6 + 4;
        for (int i = cx - radiusX; i <= cx + radiusX; i++) {
            for (int j = cy - radiusY; j <= cy + radiusY; j++) {
                float wobble = noise(i * 0.05, j * 0.05) * 0.8f;
                float dx = (float)(i - cx) / radiusX;
                float dy = (float)(j - cy) / radiusY;
                for (int k = cz + radiusZ * 2; k >= cz - radiusZ * 2; k--) {
                    float dz = (float)(k - cz) / radiusZ + wobble;
                    if (dz * dz + (dx * dx + dy * dy) > 1.0f)
                        continue;
                    // Unlike the reeds' lumps, these cover interiors too.
                    if (getBlock(i, j, k, zone).isAboveWater())
                        setBlock(i, j, k,
                                 Block(plasma::Vector<3, unsigned char>(rockColor(i, j, k, zone)),
                                       BlockBuilding | (1 << BlockFlagDestructible)),
                                 zone);
                }
            }
        }
    }

    // The world's start: a statue at the spawn point of a nameless world.
    if (m_name.empty() && x == (int)m_spawnPoint[0] / 256 && y == (int)m_spawnPoint[1] / 256) {
        Field *ground = getField((int)m_spawnPoint[0], (int)m_spawnPoint[1], zone);
        if (ground) {
            int top = ground->m_baseZ + ground->m_size;
            Sprite *statue = m_spriteManager.sprite(SpriteCharacterPlatform);
            plasma::Vector<4, int> placementMargins(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f));
            plasma::Vector<3, int> position((int)m_spawnPoint[0] - statue->m_size[0] / 2,
                                            (int)m_spawnPoint[1] - statue->m_size[1] / 2, top);
            placeSprite(statue, position, 0, 6, PlacementPlain, zone, true, placementMargins, 0);
        }
    }
    // Every other zone tries to put up a camp.
    if ((x + y) % 2 != 0) {
        for (int attempt = 0; attempt < CampPlacementAttempts; attempt++) {
            Fixed positionY = Fixed(baseY + CampPlacementMargin + rand() % CampPlacementRandomRange) - Fixed(-0.5);
            Fixed positionX = Fixed(baseX + CampPlacementMargin + rand() % CampPlacementRandomRange) - Fixed(-0.5);
            plasma::Vector<3, Fixed> position(positionX, positionY, Fixed((long long)0));
            if (record.kind == Region::Record::SceneryCity || record.kind == Region::Record::SceneryRuins) {
                float inside = 1.0f - record.shapeDistance(position[0], position[1]);
                if (inside > 0.0f && inside * inside > 0.0f)
                    continue;
            }
            Field *ground = getField((int)position[0], (int)position[1], zone);
            if (ground)
                position[2] = Fixed(ground->m_baseZ + ground->m_size);
            if (placeCamp(zone, position)) {
                structures.push_back(position);
                break;
            }
        }
    }
    // Mounds of earth.
    int mounds = 0;
    {
        int cellType = region->m_cells[cellIndex].kind;
        if (cellType != Region::Cell::StructurePortal && cellType != Region::Cell::StructureQuarter &&
            cellType != Region::Cell::StructureDungeon) {
            mounds = rand() % MoundCountRange;
            if (temperature > 0.6f || temperature < 0.3f) {
                if (humidity > 0.7f || (humidity < 0.4f && (humidity > 0.2f || temperature < 0.8f)))
                    mounds += 10;
            }
        }
    }
    for (; mounds > 0; mounds--) {
        int radiusX = rand() % MoundRadiusRange + MoundMinimumRadius;
        int radiusY = rand() % MoundRadiusRange + MoundMinimumRadius;
        int radiusZ = rand() % MoundRadiusRange + MoundMinimumRadius;
        int px = rand() % ((64 - radiusX) * 4) + (x * (BlocksPerZone / 2) + radiusX) * 2;
        int py = rand() % ((64 - radiusY) * 4) + (y * (BlocksPerZone / 2) + radiusY) * 2;
        float sx = px - m_spawnPoint[0];
        // Mound spawn/structure spacing, scenery gate and noise outline.
        float sy = py - m_spawnPoint[1];
        if (sx * sx + sy * sy < 400.0f)
            continue;
        bool crowded = false;
        for (std::list<plasma::Vector<3, Fixed>>::iterator it = structures.begin(); it != structures.end(); ++it) {
            float dx = (float)(Fixed(px) - (*it)[0]);
            float dy = (float)(Fixed(py) - (*it)[1]);
            if (dx * dx + dy * dy < 1600.0f) {
                crowded = true;
                break;
            }
        }
        if (crowded)
            continue;
        if (recordInfluence(px, py) > 0.25f)
            continue;
        Field *ground = getField(px, py, zone);
        int top = ground->m_baseZ + ground->m_size;
        for (int i = ground->m_size - 1; i >= 0; i--, top--) {
            if (ground->getMutableBlock(i).isSolid())
                break;
        }
        int below = getBlock(px, py, top - 1, zone).type & BlockTypeMask;
        if (below == BlockRoad || below == BlockLeaves || below == BlockTree)
            continue;
        if (getBlock(px, py, top - 1, zone).type & (1 << BlockFlagInterior))
            continue;
        for (int i = px - radiusX; i <= px + radiusX; i++) {
            for (int j = py - radiusY; j <= py + radiusY; j++) {
                float wobble = noise(i * 0.05, j * 0.05) * 0.8f;
                float dx = (float)(i - px) / radiusX;
                float dy = (float)(j - py) / radiusY;
                for (int k = top + radiusZ * 2; k >= top - radiusZ * 2; k--) {
                    float dz = (float)(k - top) / radiusZ + wobble;
                    if (dz * dz + (dx * dx + dy * dy) > 1.0f)
                        continue;
                    if (!(getBlock(i, j, k, zone).type & (1 << BlockFlagInterior)))
                        setBlock(i, j, k,
                                 Block(plasma::Vector<3, unsigned char>(rockColor(i, j, k, zone)),
                                       BlockBuilding | (1 << BlockFlagDestructible)),
                                 zone);
                }
            }
        }
    }
    // Settlements.
    if (record.kind == Region::Record::SceneryCity || record.kind == Region::Record::SceneryRuins)
        generateHouse(zone, &record);
    // A ring of earth pillars around a cell of type 4, with a shrine.
    if (region->m_cells[cellIndex].kind == Region::Cell::StructurePortal) {
        int cx = baseX + (BlocksPerZone / 2);
        int cy = baseY + (BlocksPerZone / 2);
        Field *ground = getField(cx, cy, zone);
        int top = ground->m_baseZ + ground->m_size;
        plasma::Vector<3, Fixed> center = plasma::Vector<3, Fixed>(Fixed(cx), Fixed(cy), Fixed(top));
        structures.push_back(center);
        int pillars = rand() % PortalPillarCountRange + PortalMinimumPillars;
        double count = pillars;
        for (int i = 0; pillars > 0; pillars--, i += 2) {
            float angle = (float)(i * 3.141592653589793 / count);
            float s = sin(angle);
            float c = cos(angle);
            plasma::Vector<3, int> base(cx + (int)(c * 25.0f), cy + (int)(s * 25.0f), top);
            Field *column = getField(base[0], base[1], zone);
            if (column)
                base[2] = column->m_baseZ + column->m_size;
            base[2] += PortalPillarBaseLift;
            int radiusX = rand() % 4 + 3;
            int radiusY = rand() % 4 + 3;
            for (int px = base[0] - radiusX * 2; px <= base[0] + radiusX * 2; px++) {
                // Portal pillar outline: 0.05 noise deforms normalized cross-sections by amplitude 0.3.
                for (int py = base[1] - radiusY * 2; py <= base[1] + radiusY * 2; py++) {
                    // Both coordinates of this noise are y.
                    float wobble = noise(py * 0.05, py * 0.05) * 0.3f;
                    for (int pz = base[2] + PortalPillarVerticalSampleExtent;
                         pz >= base[2] - PortalPillarVerticalSampleExtent; pz--) {
                        float dz = (float)(pz - base[2]) / 10.0f;
                        float dy = (float)(py - base[1]) / radiusY + wobble;
                        float dx = (float)(px - base[0]) / radiusX + noise(px * 0.05, pz * 0.05) * 0.3f;
                        if (dy * dy + dx * dx + dz * dz > 1.0f)
                            continue;
                        if (!(getBlock(px, py, pz, zone).type & (1 << BlockFlagInterior)))
                            setBlock(
                                px, py, pz,
                                Block(plasma::Vector<3, unsigned char>(rockColor(px, py, pz, zone)), BlockBuilding),
                                zone);
                    }
                }
            }
        }
        // Rune-stone layout: placement offset and per-axis object dimensions.
        Zone::Object shrine;
        shrine.position = center + plasma::Vector<3, Fixed>(Fixed(3.5f), Fixed(3.5f), Fixed((long long)0));
        while (!getBlock(shrine.position[0], shrine.position[1], shrine.position[2], zone).isSolid())
            shrine.position[2] -= Fixed(1);
        while (getBlock(shrine.position[0], shrine.position[1], shrine.position[2], zone).isSolid())
            shrine.position[2] += Fixed(1);
        shrine.type = StaticObjectRuneStone;
        shrine.rotation = rand() % 4;
        shrine.size = plasma::Vector<3, float>(4.0f, 4.0f, 5.0f);
        zone->m_objects.push_back(shrine);
    }

    // Trees on a jittered grid of 14 x 14 places.
    for (int gx = 0, cx = baseX; gx < 14; gx++, cx += 18) {
        for (int gy = 0, cy = baseY + TreeGridRows; gy < 14; gy++, cy += 18) {
            float forest = 0.0f;
            if (record.kind == Region::Record::SceneryForest) {
                forest = 1.0f - record.shapeDistance(Fixed(cx + TreeGridSampleCenterOffset), Fixed(cy));
                if (forest <= 0.0f)
                    forest = 0.0f;
                else
                    forest *= forest;
            }
            // Tree silhouette distribution: radius and height depend on humidity and forest influence.
            float humidity = terrainHumidity(cx + TreeHumiditySampleOffset, cy, zone);
            int radius = (int)(rand() % 5 + (humidity * 2.0f + 6.0f) + forest * 4.0f);
            int height = (int)((rand() * 8.0f / float(RAND_MAX) + (forest * 6.0f + 8.0f)) * (humidity * 0.5f + 1.0f));
            int py = cy - TreeGridRowInset + radius;
            int px = cx + radius;
            // Not within 20 blocks of the spawn point.
            float sx = px - m_spawnPoint[0];
            float sy = py - m_spawnPoint[1];
            if (sx * sx + sy * sy < 400.0f)
                continue;
            if (px > baseX + 256 - radius / 2)
                px = baseX + 256 - radius / 2;
            if (py > baseY + 256 - radius / 2)
                py = baseY + 256 - radius / 2;
            bool crowded = false;
            for (std::list<plasma::Vector<3, Fixed>>::iterator it = structures.begin(); it != structures.end(); ++it) {
                float dx = (float)(Fixed(py) - (*it)[1]);
                float dy = (float)(Fixed(px) - (*it)[0]);
                if (dx * dx + dy * dy < 1600.0f) {
                    crowded = true;
                    break;
                }
            }
            if (crowded)
                // Tree-kind climate/noise/chance policy.
                continue;
            float variety = noise(m_treeKindNoiseOffsets[0] + px * 0.001, m_treeKindNoiseOffsets[1] + py * 0.001);
            int kind = rand() % 2 != 0 ? 5 : 0;
            if (variety > 0.3f) {
                if (rand() % 10 != 0)
                    kind = 1;
            } else if (rand() % 10 == 0) {
                kind = 1;
            }
            if (rand() % 10 == 0)
                kind = 2;
            float temperature = terrainTemperature(px, py, zone);
            if (temperature > 0.8f && humidity > 0.7f) {
                kind = (rand() % 2 != 0) + 4;
                if (rand() % 4 == 0)
                    kind = 3;
            }
            bool tall = false;
            if (temperature < 0.3f) {
                if (temperature > 0.2f && noise(px * 0.001 + 8473.0, py * 0.001 + 9438.0) > 0.6f && rand() % 5 != 0)
                    tall = true;
                else
                    kind = 1;
            } else if (temperature < 0.7f && noise(px * 0.001 + 8473.0, py * 0.001 + 9438.0) > 0.8f &&
                       rand() % 5 != 0) {
                tall = true;
            }
            if (tall) {
                kind = 2;
                height += rand() % (height / 2);
            } else if (kind == 1) {
                if ((float)radius > height * 0.5f)
                    radius = (int)(height * 0.5f);
                if (radius < 1)
                    radius = 1;
            } else if (kind == 2) {
                height += rand() % (height / 2);
            }
            terrainHumidity(px, py, zone);
            if (px + radius > baseX + 256)
                px = baseX - radius + 256;
            if (py + radius > baseY + 256)
                py = baseY - radius + 256;
            // Forests grow denser towards their record's centre.
            float density = vegetation(px, py, zone) + forest;
            if (rand() / float(RAND_MAX) > density)
                continue;
            Field *column = getField(px, py, zone);
            if (!column)
                continue;
            int z = column->m_baseZ + column->m_size - 1;
            while (!getBlock(px, py, z, zone).isSolid())
                z--;
            if (z < 0)
                continue;
            int ground = getBlock(px, py, z, zone).type & BlockTypeMask;
            z++;
            if (terrainTemperature(px, py, zone) > 0.8f && terrainHumidity(px, py, zone) < 0.2f)
                kind = 3;
            if (ground == BlockGrass || ground == BlockSnow || (ground == BlockSand && z <= 2))
                buildTree(px, py, z, radius < 1 ? 1 : radius, height, kind, zone);
        }
    }
    // A signpost in every other zone.
    if ((x + y) % 2 == 0) {
        int px = baseX + SignpostZoneMargin + rand() % SignpostZoneRandomRange;
        int py = baseY + 16 + rand() % 224;
        Field *column = getField(px, py, zone);
        if (column) {
            int z = column->m_baseZ + column->m_size;
            while (!getBlock(px, py, z, zone).isSolid())
                z--;
            Zone::Object sign;
            sign.type = StaticObjectStatue;
            // Signpost and street-lamp model dimensions by object type.
            sign.position = plasma::Vector<3, Fixed>(Fixed(px) - Fixed(-0.5), Fixed(py) - Fixed(-0.5), Fixed(z + 1));
            sign.size = plasma::Vector<3, float>(2.0f, 2.0f, 8.0f);
            sign.rotation = rand() % 4;
            zone->m_objects.push_back(sign);
        }
    }
    // The record's creatures, on a grid inside its shape.
    if (record.kind != Region::Record::SceneryNone && record.kind != Region::Record::SceneryPortal &&
        record.kind != Region::Record::SceneryCastle && record.kind != Region::Record::SceneryCity &&
        record.kind != Region::Record::SceneryRuins) {
        std::vector<plasma::Vector<3, Fixed>> places;
        for (int i = 0, gi = 0; i < EncounterLatticeExtent; i += EncounterLatticeStep, gi++) {
            for (int j = 0, step = gi; j < EncounterLatticeExtent;
                 j += EncounterLatticeStep, step += EncounterLatticeColumnStep) {
                if (step % EncounterLatticeSelectionModulus != 0)
                    continue;
                int px = zone->m_position[0] * 256 + EncounterZoneInset + i;
                int py = zone->m_position[1] * 256 + EncounterZoneInset + j;
                float inside = 1.0f - record.shapeDistance(Fixed(px), Fixed(py));
                if (0.0f >= inside)
                    inside = 0.0f;
                else
                    inside = inside * inside;
                if (rand() / float(RAND_MAX) > inside * 0.75f)
                    continue;
                Field *column = getField(px, py, zone);
                if (!column)
                    continue;
                int z = column->m_groundZ;
                while (getBlock(px, py, z, zone).isSolid())
                    z++;
                places.push_back(plasma::Vector<3, Fixed>(Fixed(px) - Fixed(-0.5), Fixed(py) - Fixed(-0.5), Fixed(z)));
            }
        }
        populateRecord(zone, &record, places, 5);
    }
    // A record of kind 9 keeps one creature at its centre.
    if (record.kind == Region::Record::SceneryCave) {
        int cx = (int)record.center[0];
        int cy = (int)record.center[1];
        if (plasma::Vector<2, int>(cx / 256, cy / 256) == zone->m_position) {
            Spawn *keeper = new Spawn;
            keeper->position[0] = record.center[0];
            keeper->position[1] = record.center[1];
            keeper->position[2] = Fixed(getField((int)keeper->position[0], (int)keeper->position[1], zone)->m_baseZ);
            keeper->hostilityType = HostilityEnemy;
            keeper->race = RaceSeaCrab;
            keeper->level = record.level;
            keeper->rarity = (unsigned char)record.rarity;
            zone->m_spawns.push_back(keeper);
        }
    }
    // The region's populated cells by distance from this zone, nearest first; nothing reads the list
    // after the sort.
    std::vector<CellDistance> nearby;
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            if (!region->m_cells[i * 64 + j].kind)
                continue;
            plasma::Vector<2, int> cell(regionX * ZonesPerRegion + i, regionY * ZonesPerRegion + j);
            nearby.push_back(CellDistance(cell, (cell - plasma::Vector<2, int>(x, y)).squaredLength()));
        }
    }
    std::sort(nearby.begin(), nearby.end(),
              [](const CellDistance &a, const CellDistance &b) { return a.distanceSquared < b.distanceSquared; });
    // Wild creatures and their groups, at most one per ninth of the zone.
    for (int gi = 0; gi < WildCreatureGridWidth; gi++) {
        for (int gj = 0; gj < WildCreatureGridHeight; gj++) {
            if (rand() % 4 == 0)
                continue;
            int px = baseX + WildGroupGridInset + gi * WildGroupGridStep + rand() % WildGroupGridJitterRange;
            int py = baseY + WildGroupGridInset + gj * WildGroupGridStep + rand() % WildGroupGridJitterRange;
            if (record.kind != Region::Record::SceneryNone && record.kind != Region::Record::SceneryPortal) {
                float inside = 1.0f - record.shapeDistance(Fixed(px), Fixed(py));
                if (inside > 0.0f && inside * inside > 0.3f)
                    continue;
            }
            float temperature = terrainTemperature(px, py, zone);
            if (temperature < 0.2f && rand() % WildGroupColdRejectionDenominator == 0)
                continue;
            float humidity = terrainHumidity(px, py, zone);
            if (humidity < 0.2f && rand() % 4 == 0)
                continue;
            bool crowded = false;
            for (std::list<plasma::Vector<3, Fixed>>::iterator it = structures.begin(); it != structures.end(); ++it) {
                float dx = (float)(Fixed(px) - (*it)[0]);
                float dy = (float)(Fixed(py) - (*it)[1]);
                if (dx * dx + dy * dy < 400.0f) {
                    crowded = true;
                    break;
                }
            }
            if (crowded)
                continue;
            int z = getField(px, py, zone)->m_groundZ;
            while (getBlock(px, py, z, zone).isSolid())
                z++;
            int ground = getBlock(px, py, z - 1, zone).type & BlockTypeMask;
            if (recordInfluence(px, py) > 0.0f)
                continue;
            if (roadDistance(px, py, zone) < 1.0f)
                continue;
            Spawn *leader = new Spawn;
            leader->position = plasma::Vector<3, Fixed>(Fixed(px) + Fixed(0.5f), Fixed(py) + Fixed(0.5f), Fixed(z));
            leader->rotation = rand() * 360.0f / float(RAND_MAX);
            leader->level = 1;
            leader->race = randomCreatureRace(px, py, z, 0);
            if (ground == BlockRock) {
                leader->race = rand() % 2 != 0 ? RaceFireScrub : RaceThornTree;
                leader->hostilityType = HostilityAttackableObject;
                zone->m_spawns.push_back(leader);
                continue;
            }
            if (z >= 0) {
                if (ground == BlockGrass || ground == BlockGround || ground == BlockSand) {
                    if (rand() % HarvestablePlantChanceDenominator != 0 && temperature > 0.8f && humidity < 0.1f) {
                        switch (rand() % 2) {
                        case 0:
                            leader->race = RaceScrub;
                            break;
                        case 1:
                            leader->race = RaceCactus;
                            break;
                        }
                        leader->hostilityType = HostilityAttackableObject;
                        zone->m_spawns.push_back(leader);
                        continue;
                    }
                    if (ground == BlockGrass && rand() % 3 != 0 && terrainHumidity(px, py, zone) > 0.1f) {
                        switch (rand() % 4) {
                        case 1:
                            leader->race = RaceCottonPlant;
                            break;
                        case 2:
                            leader->race = RaceGinseng;
                            break;
                        case 3:
                            leader->race = RaceCobwebScrub;
                            break;
                        default:
                            leader->race = RaceBush;
                            break;
                        }
                        leader->multipliers.hp = 25.0f;
                        leader->hostilityType = HostilityAttackableObject;
                        zone->m_spawns.push_back(leader);
                        continue;
                    }
                } else if (ground == BlockSnow && rand() % HarvestablePlantKindChoiceCount == 0) {
                    switch (rand() % 4) {
                    case 1:
                        leader->race = RaceCottonPlant;
                        break;
                    case 2:
                        leader->race = RaceCobwebScrub;
                        break;
                    case 3:
                        leader->race = RaceSnowBerryBush;
                        break;
                    default:
                        leader->race = RaceSnowBush;
                        break;
                    }
                    leader->multipliers.hp = 25.0f;
                    leader->hostilityType = HostilityAttackableObject;
                    zone->m_spawns.push_back(leader);
                    continue;
                }
            }
            if (leader->hostilityType == HostilityEnemy &&
                !(leader->appearance.appearanceFlags & (1 << AppearanceFlagMissionLevel)) && rand() % 100 == 0)
                leader->appearance.appearanceFlags |= (1 << AppearanceFlagTinted);
            Region::Record *square = getRecord((int)leader->position[0] / 256 / ZonesPerSceneryRecord,
                                               (int)leader->position[1] / 256 / ZonesPerSceneryRecord);
            if (leader->hostilityType != HostilityAttackableObject) {
                int minimum;
                int maximum;
                raceLevelRange(leader->race, minimum, maximum);
                leader->level = rand() % (maximum - minimum + 1) + minimum;
                if (square && minimum <= square->level && maximum >= square->level) {
                    float inside = 1.0f - square->shapeDistance(leader->position[0], leader->position[1]);
                    if (inside > 0.0f && inside * inside > 0.0f)
                        leader->rarity = (unsigned char)square->rarity;
                }
            }
            if (leader->hostilityType == HostilityEnemy) {
                // Active from 6:00 to 24:00 (milliseconds of the day).
                leader->activeFrom = 21600000;
                leader->activeUntil = 86400000;
            }
            zone->m_spawns.push_back(leader);
            int minimum;
            int maximum;
            raceGroupSizeRange(leader->race, minimum, maximum);
            int followers = rand() % (maximum - minimum + 1) + minimum - 1;
            for (int i = 0; i < followers; i++) {
                Spawn *follower = new Spawn;
                follower->hostilityType = HostilityEnemy;
                float angle = (float)(i * 6.283185307179586 / followers);
                plasma::Vector<3, Fixed> offset(Fixed(cos(angle) * 8.0f), Fixed(sin(angle) * 8.0f),
                                                Fixed((long long)0));
                follower->position = leader->position + offset;
                follower->race = randomRaceVariant((int)follower->position[0], (int)follower->position[1],
                                                   (int)follower->position[2], leader->race);
                follower->appearance.appearanceFlags &= ~(1 << AppearanceFlagTinted);
                if (follower->hostilityType != HostilityAttackableObject) {
                    raceLevelRange(follower->race, minimum, maximum);
                    follower->level = rand() % (maximum - minimum + 1) + minimum;
                    follower->rarity = leader->rarity;
                }
                follower->rotation = rand() * 360.0f / float(RAND_MAX);
                zone->m_spawns.push_back(follower);
            }
        }
    }
    // Decoration of the surface blocks: lamps beside paved paths, reeds on wet ground, water plants,
    // plants on the grass, sand, snow and underwater ground, and materials lying on grass and snow.
    for (int bx = baseX; bx < baseX + 256; bx++) {
        for (int by = baseY; by < baseY + 256; by++) {
            Field *column = getField(bx, by, zone);
            for (int i = 0; i < column->size(); i++) {
                if (i < column->size() - 1 && column->getMutableBlock(i + 1).isSolid())
                    continue;
                int type = column->getMutableBlock(i).blockType();
                int above = column->getMutableBlock(i + 1).blockType();
                int z = column->getBaseZ() + i + 1;
                if (above == BlockAir && type == BlockRoad) {
                    // A street lamp on some paved columns near a record's centre, where seven blocks above are free.
                    if (recordInfluence(bx, by) <= 0.75f ||
                        (by * RoadLampLatticeYPhase + bx) % RoadLampLatticeModulus != 0 ||
                        rand() % RoadLampChanceDenominator != 0)
                        continue;
                    bool blocked = false;
                    for (int k = 0; k < 7; k++) {
                        if (getBlock(bx, by, z + k, zone).isSolid()) {
                            blocked = true;
                            break;
                        }
                    }
                    if (blocked)
                        continue;
                    Zone::Object lamp;
                    if (terrainTemperature(bx, by, zone) > 0.8f) {
                        lamp.type = StaticObjectFireStreetLight;
                        lamp.size = plasma::Vector<3, float>(1.0f, 1.0f, 8.0f);
                    } else {
                        lamp.type = StaticObjectStreetLight;
                        lamp.size = plasma::Vector<3, float>(2.0f, 2.0f, 8.0f);
                    }
                    lamp.position = plasma::Vector<3, Fixed>(bx, by, z);
                    lamp.rotation = rand() % 4;
                    zone->m_objects.push_back(lamp);
                } else if (above == BlockAir && type == BlockWet) {
                    // Reeds on wet ground.
                    if (terrainTemperature(bx, by, zone) <= 0.2f)
                        // Surface decoration noise channels, thresholds and probability denominators.
                        continue;
                    if (fabs(noise(bx * 0.05f + 9843.0f, by * 0.05f + 8437.0f)) <= 0.5f || rand() % 8 != 0)
                        continue;
                    Zone::Plant reed;
                    reed.position[0] = Fixed(bx) + 0.5;
                    reed.position[1] = Fixed(by) + 0.5;
                    reed.position[2] = z;
                    reed.scale = 0.09f;
                    reed.rotation = rand() * 360.0f / float(RAND_MAX);
                    reed.type = PlantReed;
                    reed.flags = (1 << PlantFlagSway);
                    zone->m_plants.push_back(reed);
                } else if (above == BlockAir && type == BlockWater) {
                    // Plants floating on the water surface above ground.
                    if (terrainTemperature(bx, by, zone) <= 0.2f || z <= 0)
                        continue;
                    if (fabs(noise(bx * 0.05f + 24234.0f, by * 0.05f + 53565.0f)) <= 0.7f || rand() % 10 != 0)
                        continue;
                    Zone::Plant lily;
                    lily.position[0] = Fixed(bx) + 0.5;
                    lily.position[1] = Fixed(by) + 0.5;
                    lily.position[2] = Fixed(z) - 0.1;
                    lily.scale = 0.09f;
                    lily.rotation = rand() * 360.0f / float(RAND_MAX);
                    lily.type = rand() % 2 + PlantWaterLily01;
                    zone->m_plants.push_back(lily);
                } else if (type == BlockGrass || type == BlockSand || type == BlockRock || type == BlockSnow) {
                    if (fabs(noise(bx * 0.05f + 9843.0f, by * 0.05f + 8437.0f)) <= 0.6f || rand() % 8 != 0)
                        continue;
                    if (z > 0) {
                        if (type == BlockRock) {
                            Zone::Plant plant;
                            plant.position[0] = Fixed(bx) + 0.5;
                            plant.position[1] = Fixed(by) + 0.5;
                            plant.position[2] = z;
                            plant.rotation = (float)(rand() % 4 * 90);
                            // Surface decoration noise channels, thresholds and probability denominators.
                            plant.scale = 0.075f;
                            plant.type = noise(bx * 0.01 + 9843.0, by * 0.01 + 8437.0) > 0.0f ? int(PlantLavaGrass)
                                                                                              : int(PlantThornPlant);
                            if (plant.type == PlantLavaGrass)
                                plant.flags |= (1 << PlantFlagSway);
                            else if (plant.type == PlantThornPlant)
                                plant.scale = (float)(rand() * 0.02 / double(RAND_MAX) + 0.03);
                            zone->m_plants.push_back(plant);
                            continue;
                        }
                        if (type != BlockSnow) {
                            if (terrainTemperature(bx, by, zone) > 0.75f) {
                                if (terrainHumidity(bx, by, zone) > 0.25f) {
                                    Zone::Plant plant;
                                    plant.position[0] = Fixed(bx) + 0.5;
                                    plant.position[1] = Fixed(by) + 0.5;
                                    plant.position[2] = z;
                                    plant.rotation = rand() * 360.0f / float(RAND_MAX);
                                    plant.scale = 0.075f;
                                    if (noise(bx * 0.01 + 9843.0, by * 0.01 + 8437.0) > 0.0f)
                                        plant.type = noise(bx * 0.01f + 34234.0f, by * 0.01f + 234234.0f) > 0.0f
                                                         ? int(PlantGrass3)
                                                         : int(PlantGrass2);
                                    else
                                        plant.type = noise(bx * 0.01 + 34234.0, by * 0.01 + 234234.0) > 0.5f
                                                         ? int(PlantEchinacea2)
                                                         : int(PlantLeaf);
                                    if (plant.type == PlantGrass || plant.type == PlantGrass2)
                                        plant.flags |= (1 << PlantFlagSway);
                                    if (plant.type == PlantEchinacea2) {
                                        plant.flags |= (1 << PlantFlagSway);
                                        plant.scale = rand() * 0.05f / float(RAND_MAX) + 0.05f;
                                    }
                                    if (plant.type == PlantLeaf) {
                                        plant.flags |= (1 << PlantFlagSway);
                                        plant.scale = rand() * 0.02f / float(RAND_MAX) + 0.1f;
                                    }
                                    if (type == BlockGrass || plant.type == PlantGrass || plant.type == PlantGrass2)
                                        // Surface decoration noise channels, thresholds and probability denominators.
                                        zone->m_plants.push_back(plant);
                                } else if (rand() % 100 == 0) {
                                    Zone::Plant plant;
                                    plant.position[0] = Fixed(bx) + 0.5;
                                    plant.position[1] = Fixed(by) + 0.5;
                                    plant.position[2] = z;
                                    plant.scale = 0.075f;
                                    plant.rotation = (float)(rand() % 4 * 90);
                                    plant.type = rand() % 2 ? int(PlantDesertFlower01) : int(PlantDesertFlower02);
                                    zone->m_plants.push_back(plant);
                                }
                            } else {
                                Zone::Plant plant;
                                plant.position[0] = Fixed(bx) + 0.5;
                                plant.position[1] = Fixed(by) + 0.5;
                                plant.position[2] = z;
                                plant.rotation = (float)(rand() % 4 * 90);
                                plant.scale = 0.075f;
                                if (terrainHumidity(bx, by, zone) > 0.5f) {
                                    if (noise(bx * 0.01 + 9843.0, by * 0.01 + 8437.0) > 0.0f)
                                        plant.type = noise(bx * 0.01f + 34234.0f, by * 0.01f + 234234.0f) > 0.0f
                                                         ? int(PlantLeaf)
                                                         : int(PlantGrass3);
                                    else
                                        plant.type = noise(bx * 0.01 + 34234.0, by * 0.01 + 234234.0) > 0.0f
                                                         ? int(PlantFlowers2)
                                                         : int(PlantFlowers);
                                } else {
                                    if (noise(bx * 0.01 + 9843.0, by * 0.01 + 8437.0) > 0.0f)
                                        plant.type = noise(bx * 0.01f + 34234.0f, by * 0.01f + 234234.0f) > 0.0f
                                                         ? int(PlantGrass)
                                                         : int(PlantGrass2);
                                    else
                                        plant.type = noise(bx * 0.01 + 34234.0, by * 0.01 + 234234.0) > 0.0f
                                                         ? int(PlantFlowers2)
                                                         : int(PlantFlowers);
                                }
                                if (plant.type == PlantGrass || plant.type == PlantGrass2 ||
                                    plant.type == PlantGrass3 || plant.type == PlantLeaf) {
                                    plant.flags |= (1 << PlantFlagSway);
                                    if (plant.type == PlantLeaf)
                                        plant.scale = rand() * 0.02f / float(RAND_MAX) + 0.1f;
                                }
                                if (type == BlockGrass || plant.type == PlantGrass || plant.type == PlantGrass2 ||
                                    plant.type == PlantGrass3)
                                    zone->m_plants.push_back(plant);
                            }
                        }
                        // Materials to pick up: mushrooms and heartflowers on grass, frozen heartflowers on snow. Surface
                        // decoration noise channels, thresholds and probability denominators.
                        if (type == BlockGrass) {
                            if (rand() % 150 != 0)
                                continue;
                            Zone::ZoneItem flower;
                            flower.item.category = ItemCategoryMaterial;
                            flower.position[0] = Fixed(bx) + 0.5;
                            flower.position[1] = Fixed(by) + 0.5;
                            flower.position[2] = z;
                            flower.rotation = rand() / float(RAND_MAX) * 360.0f;
                            switch (rand() % 2) {
                            case 0:
                                flower.item.id = MaterialItemMushroom;
                                break;
                            case 1:
                                flower.item.id = MaterialItemHeartflower;
                                flower.scale = 0.1f;
                                break;
                            }
                            flower.item.level = 1;
                            flower.flags = (1 << ZoneItemFlagGeneratedMaterial);
                            zone->m_items.push_back(flower);
                        } else if (type == BlockSnow) {
                            if (rand() % 300 != 0)
                                continue;
                            Zone::ZoneItem flower;
                            flower.item.category = ItemCategoryMaterial;
                            flower.position[0] = Fixed(bx) + 0.5;
                            flower.position[1] = Fixed(by) + 0.5;
                            flower.position[2] = z;
                            flower.rotation = rand() / float(RAND_MAX) * 360.0f;
                            switch (rand() % 2) {
                            case 0:
                            case 1:
                                flower.item.id = MaterialItemFrozenHeartflower;
                                flower.scale = 0.1f;
                                break;
                            }
                            flower.item.level = 1;
                            flower.flags = (1 << ZoneItemFlagGeneratedMaterial);
                            zone->m_items.push_back(flower);
                        }
                    } else if (z < -UnderwaterDecorationDepthLimit) {
                        // Plants on the ground deep under water.
                        Zone::Plant plant;
                        plant.position[0] = Fixed(bx) + 0.5;
                        plant.position[1] = Fixed(by) + 0.5;
                        plant.position[2] = z;
                        plant.rotation = (float)(rand() % 4 * 90);
                        plant.scale = 0.1f;
                        if (noise(bx * 0.01 + 9843.0, by * 0.01 + 8437.0) > 0.0f)
                            plant.type = rand() % 2 + PlantUnderwaterPlant;
                        else
                            plant.type = PlantAlga;
                        if (plant.type == PlantUnderwaterPlant) {
                            plant.scale = 0.075f;
                            plant.flags |= (1 << PlantFlagSway);
                        } else if (plant.type == PlantAlga) {
                            plant.scale = 0.1f;
                            plant.flags |= (1 << PlantFlagSway);
                        }
                        plant.scale *= rand() / float(RAND_MAX) + 1.0f;
                        zone->m_plants.push_back(plant);
                    }
                }
            }
        }
    }

    // Keys for the airships and the spawns, then the spawns' looks and gear.
    for (int i = 0; i < (int)zone->m_airships.size(); i++) {
        if (zone->m_airships[i]->id == 0)
            zone->m_airships[i]->id = generationKey(x, y, i);
    }
    for (int i = 0; i < (int)zone->m_spawns.size(); i++) {
        if (zone->m_spawns[i]->creatureId == 0)
            zone->m_spawns[i]->creatureId = generationKey(x, y, i);
        fillAppearance(zone->m_spawns[i]->race, zone->m_spawns[i]->appearance, 0);
        equip(zone->m_spawns[i], false);
        // Elite creatures (AppearanceFlagTinted) carry a coin.
        if (zone->m_spawns[i]->appearance.appearanceFlags & (1 << AppearanceFlagTinted)) {
            Item coin;
            coin.category = ItemCategoryMaterial;
            coin.id = MaterialItemSpirit;
            coin.level = (short)zone->m_spawns[i]->level;
            coin.material = (unsigned char)(rand() % (ItemMaterialWindSpirit - ItemMaterialFireSpirit + 1) +
                                            ItemMaterialFireSpirit);
            coin.rarity = ItemRarityBlue;
            zone->m_spawns[i]->inventory.add(coin);
        }
        Spawn *spawn = zone->m_spawns[i];
        bool elite = (spawn->appearance.appearanceFlags >> AppearanceFlagTinted) & 1;
        spawn->equipment.weaponRight.rarity = randomRarity(spawn->rarity, elite);
        spawn->equipment.weaponLeft.rarity = randomRarity(spawn->rarity, elite);
        spawn->equipment.shoulder.rarity = randomRarity(spawn->rarity, elite);
        spawn->equipment.chest.rarity = randomRarity(spawn->rarity, elite);
        spawn->equipment.hands.rarity = randomRarity(spawn->rarity, elite);
        spawn->equipment.feet.rarity = randomRarity(spawn->rarity, elite);
        spawn->equipment.neck.rarity = randomRarity(spawn->rarity, elite);
        if (!(spawn->appearance.appearanceFlags & (1 << AppearanceFlagTinted))) {
            spawn->equipment.weaponRight.varyLevel(0.05f, false);
            spawn->equipment.weaponLeft.varyLevel(0.05f, false);
            spawn->equipment.shoulder.varyLevel(0.05f, false);
            spawn->equipment.chest.varyLevel(0.05f, false);
            spawn->equipment.hands.varyLevel(0.05f, false);
            spawn->equipment.feet.varyLevel(0.05f, false);
            spawn->equipment.neck.varyLevel(0.05f, false);
            spawn->equipment.ringLeft.varyLevel(0.05f, false);
            spawn->equipment.ringRight.varyLevel(0.05f, false);
        }
        if (zone->m_spawns[i]->hostilityType == HostilityEnemy &&
            !(zone->m_spawns[i]->appearance.appearanceFlags & (1 << AppearanceFlagTinted)))
            rand();
    }
    // Materials lying in the zone, counted by kind; the loose ones (bit 0 of flags clear) drop until
    // they rest on a solid block. A kind of category 1 or 11 lying eight or more times becomes a type
    // 9 record at the zone centre.
    std::map<std::pair<std::pair<int, int>, int>, int> itemCounts;
    for (std::vector<Zone::ZoneItem>::iterator it = zone->m_items.begin(); it != zone->m_items.end(); it++) {
        Zone::ZoneItem &item = *it;
        itemCounts[std::make_pair(std::make_pair(item.item.category, item.item.id), item.item.material)]++;
        if (item.flags & (1 << ZoneItemFlagFixed))
            continue;
        Fixed &z = item.position[2];
        Fixed &y = item.position[1];
        Fixed &x = item.position[0];
        while (!getBlock(x, y, z - 0.5f, zone).isSolid())
            z -= Fixed(1);
    }
    for (std::map<std::pair<std::pair<int, int>, int>, int>::iterator it = itemCounts.begin(); it != itemCounts.end();
         it++) {
        if ((it->first.first.first == ItemCategoryConsumable || it->first.first.first == ItemCategoryMaterial) &&
            it->second >= 8) {
            Zone::GenerationMarker record;
            record.kind = GenerationMarkerItemGroup;
            record.item.category = it->first.first.first;
            record.item.id = it->first.first.second;
            record.item.material = it->first.second;
            record.position = plasma::Vector<3, Fixed>(center[0], center[1], 0);
            zone->m_generationMarkers.push_back(record);
        }
    }
    // Spawns present all day, counted by hostility type and race; five or more of a race of hostility
    // type 1, 5 or 6 become a type 10 record.
    std::map<std::pair<int, int>, int> groupCounts;
    for (std::vector<Spawn *>::iterator it = zone->m_spawns.begin(); it != zone->m_spawns.end(); it++) {
        Spawn *spawn = *it;
        if (spawn->activeUntil == 24 * World::MillisecondsPerHour)
            groupCounts[std::make_pair(spawn->hostilityType, spawn->race)]++;
    }
    for (std::map<std::pair<int, int>, int>::iterator it = groupCounts.begin(); it != groupCounts.end(); it++) {
        if ((it->first.first == HostilityEnemy || it->first.first == HostilityAttackableObject ||
             it->first.first == HostilityPet) &&
            it->second >= 5) {
            Zone::GenerationMarker record;
            record.kind = GenerationMarkerCreatureGroup;
            record.race = it->first.second;
            record.position = plasma::Vector<3, Fixed>(center[0], center[1], 0);
            zone->m_generationMarkers.push_back(record);
        }
    }

    zone->m_baseSpawnCount = (int)zone->m_spawns.size();
    refreshZonePopulation(zone);
    // The zone's saved changes.
    if (!m_client && !m_name.empty()) {
        std::stringstream key;
        key << "zone" << x << "_" << y;
        BytesIO savedZoneData;
        if (m_database.read(key.str(), savedZoneData))
            Serializer(&savedZoneData).loadZone(this, zone);
    }
    computeLight(baseX, baseY, baseX + 256, baseY + 256, 0, zone);
    for (std::list<Zone::Plant>::iterator it = zone->m_plants.begin(); it != zone->m_plants.end(); ++it)
        it->daylight = (float)cubeLight((int)it->position[0], (int)it->position[1], (int)it->position[2], zone, 0);
    // Every placed object joins the list of its 8 x 8 column cell.
    for (int i = 0; i < (int)zone->m_objects.size(); i++) {
        std::list<Zone::CellRecord> *cell =
            getCellObjects((int)(zone->m_objects[i].position[0] / 8), (int)(zone->m_objects[i].position[1] / 8), zone);
        if (cell)
            cell->push_back(Zone::CellRecord(&zone->m_objects[i], plasma::Vector<3, int>(x, y, i)));
    }
    region->m_zones[x % 64][y % 64] = zone;
}

// The box of a marker's voxels: from (x, y, z), which must be the box's lowest corner (no voxel of
// the colour below it in x, y or z), as far as the colour runs along each axis; both corners
// turned by the rotation like placeSprite turns voxels, ordered, and high made exclusive.
bool markerBounds(Sprite *sprite, int x, int y, int z, int rotation, plasma::Vector<3, int> *low,
                  plasma::Vector<3, int> *high)
{
    *low = plasma::Vector<3, int>(x, y, z);
    *high = plasma::Vector<3, int>(x, y, z);
    plasma::Vector<3, unsigned char> color = sprite->voxel(x, y, z);
    if (sprite->voxel(x - 1, y, z) == color)
        return false;
    if (sprite->voxel(x, y - 1, z) == color)
        return false;
    if (sprite->voxel(x, y, z - 1) == color)
        return false;
    for (int i = x; i < sprite->m_size[0]; i++) {
        if (sprite->voxel(i, y, z) != color)
            break;
        (*high)[0] = i;
    }
    for (int j = y; j < sprite->m_size[1]; j++) {
        if (sprite->voxel(x, j, z) != color)
            break;
        (*high)[1] = j;
    }
    for (int k = z; k < sprite->m_size[2]; k++) {
        if (sprite->voxel(x, y, k) != color)
            break;
        (*high)[2] = k;
    }
    int turn = rotation % 4;
    switch (turn) {
    case 1:
        *low = plasma::Vector<3, int>((*low)[1], sprite->m_size[0] - (*low)[0] - 1, (*low)[2]);
        break;
    case 2:
        *low = plasma::Vector<3, int>(sprite->m_size[0] - (*low)[0] - 1, sprite->m_size[1] - (*low)[1] - 1, (*low)[2]);
        break;
    case 3:
        *low = plasma::Vector<3, int>(sprite->m_size[1] - (*low)[1] - 1, (*low)[0], (*low)[2]);
        break;
    }
    switch (turn) {
    case 1:
        *high = plasma::Vector<3, int>((*high)[1], sprite->m_size[0] - (*high)[0] - 1, (*high)[2]);
        break;
    case 2:
        *high =
            plasma::Vector<3, int>(sprite->m_size[0] - (*high)[0] - 1, sprite->m_size[1] - (*high)[1] - 1, (*high)[2]);
        break;
    case 3:
        *high = plasma::Vector<3, int>(sprite->m_size[1] - (*high)[1] - 1, (*high)[0], (*high)[2]);
        break;
    }
    for (int i = 0; i < 3; i++) {
        if ((*low)[i] > (*high)[i])
            std::swap((*low)[i], (*high)[i]);
    }
    (*high)[0]++;
    (*high)[1]++;
    (*high)[2]++;
    return true;
}

void World::RegionRecord::generationRange(int &minimum, int &maximum)
{
    minimum = TemperateMinimumGenerationLevel;
    maximum = TemperateMaximumGenerationLevel;
    if (temperature < 0.2f) {
        minimum = ColdMinimumGenerationLevel;
        maximum = ColdMaximumGenerationLevel;
    }
    if (humidity < 0.2f && temperature > 0.8f) {
        minimum = DesertMinimumGenerationLevel;
        maximum = DesertMaximumGenerationLevel;
    }
    if (humidity > 0.8f && temperature > 0.8f) {
        minimum = JungleMinimumGenerationLevel;
        maximum = JungleMaximumGenerationLevel;
    }
    if (firelands == true) {
        minimum = FirelandsMinimumGenerationLevel;
        maximum = FirelandsMaximumGenerationLevel;
    }
}

plasma::Vector<3, float> World::soilColor(int x, int y)
// Soil palette noise parameters.
{
    float blend = (noise(x * 0.04, y * 0.04) + 1.0f) * 0.5f;
    float blue = noise(x * 0.005 + 45645.0, y * 0.005 + 456456.0) * 120.0f;
    float red = -blue;
    if (red > 0.0f)
        red = 0.0f;
    float light = noise(x * 0.02 + 89648.0, y * 0.02 + 1649.0) * 80.0f + 1.0f;
    // Procedural RGB palette and brightness coefficients for this surface.
    float green = noise(x * 0.005 + 342.0, y * 0.005 + 23423.0) * 30.0f;
    plasma::Vector<3, float> color = plasma::Vector<3, float>(light, 120.0f, light) * blend +
                                     plasma::Vector<3, float>(200.0f, 230.0f, light) * (1.0f - blend);
    color += plasma::Vector<3, float>(red, green, blue);
    return clampColor(color);
}

void World::initCompanion(long long ownerId, Creature *companion)
{
    SequentialBehavior *behavior = new SequentialBehavior;
    behavior->m_behaviors.push_back(new CombatBehavior(20.0f));
    CompanionBehavior *follow = new CompanionBehavior;
    follow->m_ownerId = ownerId;
    behavior->m_behaviors.push_back(follow);
    if (companion->m_behavior)
        delete companion->m_behavior;
    companion->m_entityData.multipliers = Creature::StatMultipliers();
    companion->m_behavior = behavior;
    switch (companion->m_entityData.race) {
    case RaceTurtle:
        companion->m_entityData.multipliers.hp = 300.0f;
        companion->m_entityData.multipliers.armor = 5.0f;
        companion->m_entityData.multipliers.resistance = 5.0f;
        companion->m_entityData.multipliers.damage = 0.1f;
        break;
    case RaceSpitter:
        companion->m_entityData.classType = ClassMage;
        companion->m_entityData.specialization = SpecializationWater;
        break;
    case RaceSnoutBeetle:
        companion->m_entityData.classType = ClassRanger;
        companion->m_entityData.specialization = SpecializationScout;
        break;
    }
    // A pet is no stronger than its owner's level and grows with the owner's first skill.
    std::map<long long, Creature *>::iterator it = m_creatures.find(ownerId);
    if (it != m_creatures.end() && it->second) {
        Creature *owner = it->second;
        if (companion->m_entityData.level > owner->m_entityData.level)
            companion->m_entityData.level = owner->m_entityData.level;
        companion->m_entityData.ownerId = ownerId;
        companion->m_entityData.multipliers.hp *=
            skillBonus(owner->m_entityData.skillLevels[SkillPetTaming]) * 0.5f + 1.0f;
        owner->m_companionId = companion->m_id;
    }
}

static float segmentDistanceSquared(const plasma::Vector<2, double> &a, const plasma::Vector<2, double> &b,
                                    const plasma::Vector<2, double> &point);

// The distance from a block column, displaced by noise, to the nearest of the roads that join its
// region's record to the four neighbouring records, scaled by 0.0002 (1 when the record is
// missing).
float World::roadNetworkDistance(int x, int y)
{
    plasma::Vector<2, int> cell = findNearestRegion(x, y);
    RegionRecord *record = getRegionRecord(cell[0], cell[1]);
    if (!record)
        return 1.0f;
    float nearest = -1.0f;
    // Road coordinate warp: fine noise weighted by 0.1 plus broad noise, scaled to 500 blocks.
    plasma::Vector<2, double> point;
    point[0] = (noise(m_coordinateWarpOffsets[2] + x * 0.01, m_coordinateWarpOffsets[3] + y * 0.01) * 0.1 +
                noise(m_coordinateWarpOffsets[0] + x * 0.0005, m_coordinateWarpOffsets[1] + y * 0.0005)) *
                   500.0 +
               x;
    point[1] = (noise(m_coordinateWarpOffsets[6] + x * 0.01, m_coordinateWarpOffsets[7] + y * 0.01) * 0.1 +
                noise(m_coordinateWarpOffsets[4] + x * 0.0005, m_coordinateWarpOffsets[5] + y * 0.0005)) *
                   500.0 +
               y;
    RegionRecord *neighbour;
    int cellX = cell[0];
    int cellY = cell[1];
    neighbour = getRegionRecord(cellX - 1, cellY);
    if (neighbour) {
        float distance = segmentDistanceSquared(plasma::Vector<2, double>(record->column),
                                                plasma::Vector<2, double>(neighbour->column), point);
        if (nearest < 0.0f || nearest > distance)
            nearest = distance;
    }
    neighbour = getRegionRecord(cellX + 1, cellY);
    if (neighbour) {
        float distance = segmentDistanceSquared(plasma::Vector<2, double>(record->column),
                                                plasma::Vector<2, double>(neighbour->column), point);
        if (nearest < 0.0f || nearest > distance)
            nearest = distance;
    }
    neighbour = getRegionRecord(cellX, cellY - 1);
    if (neighbour) {
        float distance = segmentDistanceSquared(plasma::Vector<2, double>(record->column),
                                                plasma::Vector<2, double>(neighbour->column), point);
        if (nearest < 0.0f || nearest > distance)
            nearest = distance;
    }
    neighbour = getRegionRecord(cellX, cellY + 1);
    if (neighbour) {
        float distance = segmentDistanceSquared(plasma::Vector<2, double>(record->column),
                                                plasma::Vector<2, double>(neighbour->column), point);
        if (nearest < 0.0f || nearest > distance)
            nearest = distance;
    }
    if (nearest < 0.0f)
        return 1.0f;
    return nearest * 0.0002f;
}

float World::distanceSquared(const plasma::Vector<2, int> &column, Fixed x, Fixed y)
{
    plasma::Vector<2, Fixed> offset = plasma::Vector<2, Fixed>(column) - plasma::Vector<2, Fixed>(x, y);
    return plasma::Vector<2, float>(offset).squaredLength();
}

plasma::Vector<2, float> World::landscapeOffset(int x, int y)
{
    // Landscape coordinate jitter noise and amplitude.
    plasma::Vector<2, float> offset;
    offset[0] = noise(y * 0.0005, 3423.0) * 3.0f * 256.0f;
    offset[1] = noise(x * 0.0005, 23421.0) * 3.0f * 256.0f;
    return offset;
}

// Blends the firelands flags of the surrounding three-by-three region records, using the same weights as temperature. Missing records give zero; the field stores the result.
float World::blendedFirelands(int x, int y)
{
    int minimumX = (x - BlocksPerRegion) / BlocksPerRegion;
    int minimumY = (y - BlocksPerRegion) / BlocksPerRegion;
    int maximumX = (x + BlocksPerRegion) / BlocksPerRegion;
    int maximumY = (y + BlocksPerRegion) / BlocksPerRegion;
    bool flagged = false;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.0f;
            if (record->firelands == true) {
                flagged = true;
                break;
            }
        }
    }
    if (!flagged)
        return 0.0f;
    plasma::Vector<2, float> offset = landscapeOffset(x, y);
    int offsetX = (int)(x + offset[0]);
    int offsetY = (int)(y + offset[1]);
    RegionRecord *nearest = 0;
    int nearestDistance = 0;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.0f;
            int distance = (int)distanceSquared(record->column, offsetX, offsetY);
            if (!nearest || distance < nearestDistance) {
                nearest = record;
                nearestDistance = distance;
            }
        }
    }
    if (!nearest)
        return 0.0f;
    float weights = 0.0f;
    float value = 0.0f;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.0f;
            int distance = (int)distanceSquared(record->column, offsetX, offsetY);
            float excess = (distance - nearestDistance) * 5e-7f;
            if (excess > 1.0f)
                excess = 1.0f;
            float weight = 1.0f - excess;
            weights += weight;
            if (record->firelands == true)
                value += weight;
        }
    }
    if (weights > 0.0f)
        return value / weights;
    return 0.0f;
}

static float segmentDistanceSquared(const plasma::Vector<2, double> &a, const plasma::Vector<2, double> &b,
                                    const plasma::Vector<2, double> &point)
{
    plasma::Vector<2, double> offset = point - a;
    plasma::Vector<2, double> direction = b - a;
    double lengthSquared = direction[1] * direction[1] + direction[0] * direction[0];
    if (lengthSquared < 1.0e-20f)
        return (float)(offset[0] * offset[0] + offset[1] * offset[1]);
    double fraction = offset.dot(direction) / lengthSquared;
    if (fraction <= 0.0)
        return (float)(offset[0] * offset[0] + offset[1] * offset[1]);
    if (fraction >= 1.0)
        return (float)(point - b).squaredLength();
    direction *= fraction;
    offset -= direction;
    return (float)offset.squaredLength();
}

// Marks the cells of a straight line from one cell to another as hall (type 3); where the line
// changes level, the cell above or below joins it and becomes a stair cell (bit 0).
void Dungeon::carveCorridor(const plasma::Vector<3, int> &from, const plasma::Vector<3, int> &to)
{
    plasma::Vector<3, int> delta = to - from;
    int steps = std::max(std::max(abs(delta[0]), abs(delta[1])), abs(delta[2]));
    if (steps == 0)
        return;
    plasma::Vector<3, int> previous = from;
    for (int i = 0; i <= steps; i++) {
        plasma::Vector<3, int> position = from + delta * i / steps;
        cell(position[0], position[1], position[2]).kind = DungeonCellCorridor;
        if (position[2] < previous[2]) {
            cell(position[0], position[1], position[2] + 1).kind = DungeonCellCorridor;
            cell(position[0], position[1], position[2]).flags |= (1 << DungeonCellFlagStairsUp);
        }
        if (position[2] > previous[2]) {
            cell(position[0], position[1], position[2] - 1).kind = DungeonCellCorridor;
            cell(position[0], position[1], position[2] - 1).flags |= (1 << DungeonCellFlagStairsUp);
        }
        previous = position;
    }
}

// Marks a room's cells as hall (type 3); half the rooms get a carpet (bit 1) on part of the floor,
// and on request a raised platform (type 2) inside the walls.
void Dungeon::addRoom(const plasma::Vector<3, int> &position, const plasma::Vector<3, int> &size, bool platform)
{
    if (size[0] < 1 || size[1] < 1 || size[2] < 1)
        return;
    for (int x = position[0]; x < position[0] + size[0]; x++)
        for (int y = position[1]; y < position[1] + size[1]; y++)
            for (int z = position[2]; z < position[2] + size[2]; z++)
                cell(x, y, z).kind = DungeonCellCorridor;
    if (rand() % 2) {
        int carpetX = rand() % size[0] + 1;
        int carpetY = rand() % size[1] + 1;
        int startX = position[0];
        int startY = position[1];
        if (size[0] - carpetX > 0)
            startX += rand() % (size[0] - carpetX + 1);
        if (size[1] - carpetY > 0)
            startY += rand() % (size[1] - carpetY + 1);
        for (int x = startX; x < startX + carpetX; x++)
            for (int y = startY; y < startY + carpetY; y++)
                cell(x, y, position[2]).flags |= (1 << DungeonCellFlagCarpet);
    }
    if (platform && size[0] > 2 && size[1] > 2) {
        int platformX = rand() % (size[0] - 2) + 1;
        int platformY = rand() % (size[1] - 2) + 1;
        int startX = position[0] + 1;
        int startY = position[1] + 1;
        if (size[0] - platformX > 1)
            startX += rand() % (size[0] - platformX - 1);
        if (size[1] - platformY > 1)
            startY += rand() % (size[1] - platformY - 1);
        for (int x = startX; x < startX + platformX; x++)
            for (int y = startY; y < startY + platformY; y++)
                for (int z = position[2]; z < position[2] + size[2]; z++)
                    cell(x, y, z).kind = DungeonCellStairColumn;
    }
}

// Controls how green stone becomes: the column's humidity scaled by noise and clamped from zero to one.
float World::colorBlend(int x, int y, int z, Zone *zone)
{
    // Stone green-tint noise frequencies/channels and gain/bias.
    return std::max(0.0f,
                    std::min(1.0f, terrainHumidity(x, y, zone) * ((noise(y * 0.2f + 534.0f, z * 0.2f + 13.0f) * 0.1f +
                                                                   noise(x * 0.05 + 4343.0, z * 0.1 + 84734.0)) *
                                                                      0.7f +
                                                                  0.2f)));
}

// How densely plants grow at a block column (0..1): ridged noise, thinned by rivers, roads and
// record centres, cleared or ringed by some record kinds.
float World::vegetationDensity(int x, int y, Zone *zone)
// Vegetation ridge/patch noise parameters.
{
    float ridge = noise(x * 0.01, y * 0.01);
    float edge = 1.0f - ridge * ridge;
    float patches = (float)fabs(
        noise(m_vegetationNoiseOffsets[0] + x * 0.01 + 843.0, m_vegetationNoiseOffsets[1] + y * 0.01 + 984.0) * 0.1f +
        noise(x * 0.0025 + m_vegetationNoiseOffsets[0], y * 0.0025 + m_vegetationNoiseOffsets[1]));
    float density = (1.0f - patches * ((1.0f - edge * edge) * 1.3f + 2.0f)) *
                    (noise(x * 0.005 + 94.0, y * 0.005 + 874.0) * 0.4f + 0.6f);
    Region::Record *record = getRecord(x / BlocksPerSceneryRecord, y / BlocksPerSceneryRecord);
    if (record && (record->kind == Region::Record::SceneryCanyon || record->kind == Region::Record::SceneryValley)) {
        // Dense towards the record's centre, thinning out at its edge.
        float distance = record->shapeDistance(Fixed(x), Fixed(y));
        float rise = 0.5f;
        if (density >= 1.0f)
            rise = 0.0f;
        else if (density + 0.5f > 1.0f)
            // Vegetation canyon annulus and lake/ruins clearing radii.
            rise = 1.0f - density;
        if (distance <= 0.36f) {
            float t = (1.0f - distance / 0.36f) * 1.5f;
            if (t > 1.0f)
                t = 1.0f;
            t *= t;
            density = (rise + density) * (1.0f - t) + t * 0.0f;
        } else if (distance < 1.0f) {
            float u = ((float)sqrt(distance) - 0.6f) / (1.0f - 0.6f);
            u = 1.0f - u * u;
            density += u * u * rise;
        }
    }
    if (density < 0.0f) {
        density = 0.0f;
    } else {
        float river = riverDistance(x, y, zone) * 2.0f;
        if (river > 1.0f)
            river = 1.0f;
        float dry = smoothStep(river) * density;
        float road = roadDistance(x, y, zone);
        if (road > 1.0f)
            road = 1.0f;
        float open = smoothStep(road) * dry;
        float influence = recordInfluence(x, y) * 2.0f;
        if (influence > 1.0f)
            influence = 1.0f;
        float clear = 1.0f - smoothStep(influence);
        density = clear * clear * clear * open;
    }
    if (record) {
        if (record->kind == Region::Record::SceneryMountain) {
            float inside = 1.0f - record->shapeDistance(Fixed(x), Fixed(y));
            float square = 0.0f >= inside ? 0.0f : inside * inside;
            return smoothStep(density) + square;
        }
        if (record->kind == Region::Record::SceneryPortal && record->radius > 0.0f) {
            plasma::Vector<2, float> offset(record->center - plasma::Vector<2, Fixed>(x, y));
            float ratio = offset.squaredLength() / (record->radius * record->radius);
            if (ratio < 1.0f)
                density *= 1.0f - (1.0f - ratio) * (1.0f - ratio);
        }
        if (record->kind == Region::Record::SceneryLake || record->kind == Region::Record::SceneryRuins) {
            // Vegetation canyon annulus and lake/ruins clearing radii.
            float distance = record->shapeDistance(Fixed(x), Fixed(y));
            if (distance <= 0.25f) {
                density = 0.0f;
            } else if (distance < 1.0f) {
                float q = ((float)sqrt(distance) - 0.5f) * 2.0f;
                q = 1.0f - q * q;
                density *= 1.0f - q * q;
            }
        }
    }
    return smoothStep(density);
}

#ifndef CUBE_SERVER
// The share of the region records around block column (x, y) below sea level (a negative base
// height), blended like blendedFirelands, whose body this is but for the test (0 when none of the
// 3 x 3 records is, or a record is missing). Cube only: the sky's lighting reads the weighted
// ocean share.
float World::oceanShare(int x, int y)
{
    int minimumX = (x - BlocksPerRegion) / BlocksPerRegion;
    int minimumY = (y - BlocksPerRegion) / BlocksPerRegion;
    int maximumX = (x + BlocksPerRegion) / BlocksPerRegion;
    int maximumY = (y + BlocksPerRegion) / BlocksPerRegion;
    bool ocean = false;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.0f;
            if (record->height < 0) {
                ocean = true;
                break;
            }
        }
    }
    if (!ocean)
        return 0.0f;
    plasma::Vector<2, float> offset = landscapeOffset(x, y);
    int offsetX = (int)(x + offset[0]);
    int offsetY = (int)(y + offset[1]);
    RegionRecord *nearest = 0;
    int nearestDistance = 0;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.0f;
            int distance = (int)distanceSquared(record->column, offsetX, offsetY);
            if (!nearest || distance < nearestDistance) {
                nearest = record;
                nearestDistance = distance;
            }
        }
    }
    if (!nearest)
        return 0.0f;
    float weights = 0.0f;
    float value = 0.0f;
    for (int i = minimumX; i <= maximumX; i++) {
        for (int j = minimumY; j <= maximumY; j++) {
            RegionRecord *record = getRegionRecord(i, j);
            if (!record)
                return 0.0f;
            int distance = (int)distanceSquared(record->column, offsetX, offsetY);
            float excess = (distance - nearestDistance) * 5e-7f;
            if (excess > 1.0f)
                excess = 1.0f;
            float weight = 1.0f - excess;
            weights += weight;
            if (record->height < 0)
                value += weight;
        }
    }
    if (weights > 0.0f)
        return value / weights;
    return 0.0f;
}
#endif

void World::clearRegionPopulationState(ServerUpdates *updates)
{
    if (!m_client) {
        for (int x = 0; x < 1024; ++x) {
            for (int y = 0; y < 1024; ++y) {
                Region *region = m_regions[x][y];
                if (region)
                    region->m_populated = false;
            }
        }
    }
}

namespace {
enum {
    WallPlantChanceDenominator = 5
};
enum {
    WallPlantVariantCount = 3
};
enum {
    AdapterResidentLevel = 20
};
}

// Builds a sprite into the world as blocks: the sprite's voxels turned by rotation quarter turns,
// with origin at position and the margins cropped (left, top, right, bottom before the turn). With
// foundation, the column under each bottom voxel is filled down to the ground. Empty voxels
// between a column's lowest and highest voxel become interior air. In the building styles (1..7,
// 9..15), voxels of marker colours become zone objects (doors, beds, lamps, chests and the like,
// measured from the marker's voxels), plants, or a creature, and leave interior air behind. When
// updates is given, every block placed is also queued for the clients.
void World::placeSprite(Sprite *sprite, const plasma::Vector<3, int> &position, int rotation, int blockType, int style,
                        Zone *zone, bool foundation, const plasma::Vector<4, int> &margins, ServerUpdates *updates)
{
    int sizeX = sprite->m_size[0];
    int sizeY = sprite->m_size[1];
    if (rotation % 2 != 0)
        std::swap(sizeX, sizeY);
    plasma::Vector<3, int> origin = position;
    if (zone) {
        if (origin[0] + sizeX <= zone->m_position[0] * BlocksPerZone ||
            origin[1] + sizeY <= zone->m_position[1] * BlocksPerZone ||
            origin[0] >= zone->m_position[0] * BlocksPerZone + BlocksPerZone ||
            origin[1] >= zone->m_position[1] * BlocksPerZone + BlocksPerZone)
            return;
    }
    float temperature = terrainTemperature(position[0], position[1], zone);
    int startX = 0;
    int startY = 0;
    int endX = sprite->m_size[0];
    int endY = sprite->m_size[1];
    int turn = rotation % 4;
    switch (turn) {
    case 0:
        startX = margins[0];
        startY = margins[1];
        endX -= margins[2];
        endY -= margins[3];
        break;
    case 3:
        startX = margins[1];
        startY = margins[2];
        endX -= margins[3];
        endY -= margins[0];
        break;
    case 2:
        startX = margins[2];
        startY = margins[3];
        endX -= margins[0];
        endY -= margins[1];
        break;
    case 1:
        startX = margins[3];
        startY = margins[0];
        endX -= margins[1];
        endY -= margins[2];
        break;
    }
    plasma::Vector<3, int> column;
    for (int i = startX; i < endX; i++) {
        for (int j = startY; j < endY; j++) {
            // The column's lowest and highest voxel.
            int lowest = sprite->m_size[2];
            int highest = 0;
            for (int k = 0; k < sprite->m_size[2]; k++) {
                if (sprite->voxel(i, j, k) != Sprite::s_emptyVoxel) {
                    lowest = std::min(k, lowest);
                    highest = std::max(k, highest);
                }
            }
            if (foundation && sprite->voxel(i, j, 0) != Sprite::s_emptyVoxel) {
                switch (turn) {
                case 0:
                    column = plasma::Vector<3, int>(origin[0] + i, origin[1] + j, origin[2]);
                    break;
                case 1:
                    column = plasma::Vector<3, int>(origin[0] + j, origin[1] + sprite->m_size[0] - i - 1, origin[2]);
                    break;
                case 2:
                    column = plasma::Vector<3, int>(origin[0] + sprite->m_size[0] - i - 1,
                                                    origin[1] + sprite->m_size[1] - j - 1, origin[2]);
                    break;
                case 3:
                    column = plasma::Vector<3, int>(origin[0] + sprite->m_size[1] - j - 1, origin[1] + i, origin[2]);
                    break;
                }
                Field *field = getField(column[0], column[1], zone);
                if (field) {
                    for (int z = origin[2] - 1; z > field->m_baseZ; z--) {
                        if (getBlock(column[0], column[1], z, zone).isSolid())
                            break;
                        setBlock(column[0], column[1], z, Block(sprite->voxel(i, j, 0), BlockBuilding), zone);
                    }
                }
            }
            for (int k = sprite->m_size[2] - 1; k >= 0; k--) {
                plasma::Vector<3, int> target;
                switch (turn) {
                case 0:
                    target = plasma::Vector<3, int>(origin[0] + i, origin[1] + j, origin[2] + k);
                    break;
                case 1:
                    target =
                        plasma::Vector<3, int>(origin[0] + j, origin[1] + sprite->m_size[0] - i - 1, origin[2] + k);
                    break;
                case 2:
                    target = plasma::Vector<3, int>(origin[0] + sprite->m_size[0] - i - 1,
                                                    origin[1] + sprite->m_size[1] - j - 1, origin[2] + k);
                    break;
                case 3:
                    target =
                        plasma::Vector<3, int>(origin[0] + sprite->m_size[1] - j - 1, origin[1] + i, origin[2] + k);
                    break;
                }
                plasma::Vector<3, unsigned char> &color = sprite->voxel(i, j, k);
                if (color == Sprite::s_emptyVoxel) {
                    // A hollow inside the sprite. Air/water stored RGB palette.
                    if (k > lowest && k < highest) {
                        Block air(plasma::Vector<3, unsigned char>(255, 255, 255),
                                  (1 << BlockFlagUnderwater) | (1 << BlockFlagInterior));
                        if (updates) {
                            ServerUpdates::BlockPacket packet;
                            packet.position = target;
                            packet.block = air;
                            updates->blocks.push_back(packet);
                        }
                        setBlock(target[0], target[1], target[2], air, zone);
                    }
                    continue;
                }
                // Solid blocks inside buildings stay.
                if ((getBlock(target[0], target[1], target[2], zone).type & (1 << BlockFlagInterior)) &&
                    getBlock(target[0], target[1], target[2], zone).isSolid())
                    continue;
                // Sprite marker RGB palette.
                if (style == PlacementHouse || style == PlacementInn || style == PlacementAnalyzer ||
                    style == PlacementItemShop || style == PlacementWeaponShop || style == PlacementArmorShop ||
                    style == PlacementAdapter || style == PlacementWarriorHall || style == PlacementRangerHall ||
                    style == PlacementRogueHall || style == PlacementMageHall || style == PlacementSmithy ||
                    style == PlacementCarpentersShop || style == PlacementWeavingMill) {
                    if (color == plasma::Vector<3, unsigned char>(255, 0, 0)) {
                        // A door (type 2 in style 7).
                        Zone::Object door;
                        if (objectFromVoxels(sprite, origin, rotation, i, j, k, &door)) {
                            door.type = style == PlacementAdapter ? StaticObjectBigDoor : StaticObjectDoor;
                            zone->m_objects.push_back(door);
                        }
                        Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                        if (updates) {
                            ServerUpdates::BlockPacket packet;
                            packet.position = target;
                            packet.block = air;
                            updates->blocks.push_back(packet);
                        }
                        setBlock(target[0], target[1], target[2], air, zone);
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(0, 255, 0)) {
                        Zone::Object object;
                        if (objectFromVoxels(sprite, origin, rotation, i, j, k, &object)) {
                            object.type = StaticObjectWindow;
                            zone->m_objects.push_back(object);
                        }
                        Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                        if (updates) {
                            ServerUpdates::BlockPacket packet;
                            packet.position = target;
                            packet.block = air;
                            updates->blocks.push_back(packet);
                        }
                        setBlock(target[0], target[1], target[2], air, zone);
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(0, 127, 127)) {
                        // A plant on wet, open ground now and then.
                        if (temperature <= 0.5f || getBlock(target[0], target[1], target[2], zone).isSolid())
                            continue;
                        if (rand() % WallPlantChanceDenominator == 0) {
                            Zone::Plant plant;
                            plant.type = rand() % WallPlantVariantCount + PlantIvy;
                            plant.position =
                                plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.503, 0.503, 0.003);
                            plant.scale = 0.15f;
                            plant.rotation = (float)(rotation * -90);
                            plant.flags = 0;
                            zone->m_plants.push_back(plant);
                        }
                        Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                        if (updates) {
                            ServerUpdates::BlockPacket packet;
                            packet.position = target;
                            packet.block = air;
                            updates->blocks.push_back(packet);
                        }
                        setBlock(target[0], target[1], target[2], air, zone);
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(255, 127, 0)) {
                        // Two objects of types 19 and 20 side by side.
                        Zone::Object first;
                        first.type = StaticObjectBed;
                        first.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                        // Object layout: per-axis dimensions and placement offsets for this model.
                        first.rotation = 0;
                        first.size = plasma::Vector<3, float>(2.0f, 3.0f, 1.0f);
                        zone->m_objects.push_back(first);
                        Zone::Object second;
                        second.type = StaticObjectBedTable;
                        second.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(2.25, 0.5, 0);
                        second.rotation = 0;
                        second.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.0f);
                        zone->m_objects.push_back(second);
                        if (k > lowest && k < highest) {
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(255, 127, 127)) {
                        Zone::Object object;
                        object.type = rand() % 3 + StaticObjectShelf1;
                        object.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                        object.rotation = rotation;
                        object.size = plasma::Vector<3, float>(2.0f, 3.0f, 1.0f);
                        zone->m_objects.push_back(object);
                        if (k > lowest && k < highest) {
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(127, 127, 0)) {
                        Zone::Object table;
                        table.type = StaticObjectTable;
                        table.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                        table.rotation = rand() % 4;
                        table.size = plasma::Vector<3, float>(3.0f, 3.0f, 1.0f);
                        zone->m_objects.push_back(table);
                        // Type 16 on either side of it.
                        Zone::Object seat;
                        // Object layout: per-axis dimensions and placement offsets for this model.
                        seat.type = StaticObjectStool;
                        seat.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(2.7, 0.5, 0);
                        seat.rotation = rand() % 4;
                        seat.size = plasma::Vector<3, float>(1.0f, 1.0f, 0.5f);
                        zone->m_objects.push_back(seat);
                        seat.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(-1.7, 0.5, 0);
                        zone->m_objects.push_back(seat);
                        // Drawn after the second copy is stored: it changes nothing.
                        seat.rotation = rand() % 4;
                        if (k > lowest && k < highest) {
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(255, 255, 0)) {
                        // A lamp.
                        Zone::Plant lamp;
                        lamp.type = PlantLantern02;
                        // Marker lantern warm-light RGB palette.
                        lamp.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                        lamp.color = plasma::Vector<3, float>(0.6f, 0.5f, 0.4f);
                        lamp.scale = 0.1f;
                        lamp.rotation = 0.0f;
                        lamp.flags = (1 << PlantFlagEmissive);
                        zone->m_plants.push_back(lamp);
                        if (k > lowest && k < highest) {
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(63, 0, 0)) {
                        Zone::Plant plant;
                        plant.type = PlantCrest1;
                        plant.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0.5);
                        plant.scale = 0.065f;
                        plant.rotation = (float)(rotation * -90);
                        plant.flags = 0;
                        zone->m_plants.push_back(plant);
                        if (k > lowest && k < highest) {
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(0, 0, 255)) {
                        // A plant of the style's kind, turned with the sprite.
                        if (style == PlacementInn) {
                            Zone::Plant plant;
                            plant.type = PlantInnSign;
                            plant.position =
                                plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, -0.5);
                            plant.scale = 0.1f;
                            plant.rotation = (float)((-1 - rotation) * 90);
                            plant.flags = 0;
                            zone->m_plants.push_back(plant);
                        } else if (style == PlacementAnalyzer) {
                            Zone::Plant plant;
                            plant.type = PlantIdentifierSign;
                            plant.position =
                                plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, -0.5);
                            plant.scale = 0.1f;
                            plant.rotation = (float)((-1 - rotation) * 90);
                            plant.flags = 0;
                            zone->m_plants.push_back(plant);
                        } else if (style == PlacementItemShop) {
                            Zone::Plant plant;
                            plant.type = PlantShopSign;
                            plant.position =
                                plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, -0.5);
                            plant.scale = 0.1f;
                            plant.rotation = (float)((-1 - rotation) * 90);
                            plant.flags = 0;
                            zone->m_plants.push_back(plant);
                        } else if (style == PlacementArmorShop) {
                            Zone::Plant plant;
                            plant.type = PlantArmorShopSign;
                            plant.position =
                                plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, -0.5);
                            plant.scale = 0.1f;
                            plant.rotation = (float)((-1 - rotation) * 90);
                            plant.flags = 0;
                            zone->m_plants.push_back(plant);
                        } else if (style == PlacementWeaponShop) {
                            Zone::Plant plant;
                            plant.type = PlantWeaponShopSign;
                            plant.position =
                                plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, -0.5);
                            plant.scale = 0.1f;
                            plant.rotation = (float)((-1 - rotation) * 90);
                            plant.flags = 0;
                            zone->m_plants.push_back(plant);
                        } else if (style == PlacementSmithy) {
                            Zone::Plant plant;
                            plant.type = PlantSmithySign;
                            plant.position =
                                plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, -0.5);
                            plant.scale = 0.1f;
                            plant.rotation = (float)((-1 - rotation) * 90);
                            plant.flags = 0;
                            zone->m_plants.push_back(plant);
                        } else if (style == PlacementCarpentersShop) {
                            Zone::Plant plant;
                            plant.type = PlantCarpentersshopSign;
                            plant.position =
                                plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, -0.5);
                            plant.scale = 0.1f;
                            plant.rotation = (float)((-1 - rotation) * 90);
                            plant.flags = 0;
                            zone->m_plants.push_back(plant);
                        } else if (style == PlacementWeavingMill) {
                            Zone::Plant plant;
                            plant.type = PlantWeavingmillSign;
                            plant.position =
                                plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, -0.5);
                            plant.scale = 0.1f;
                            plant.rotation = (float)((-1 - rotation) * 90);
                            plant.flags = 0;
                            zone->m_plants.push_back(plant);
                        }
                        if (k > lowest && k < highest) {
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(255, 0, 255)) {
                        if (temperature <= 0.5f)
                            continue;
                        Zone::Object object;
                        if (objectFromVoxels(sprite, origin, rotation, i, j, k, &object)) {
                            object.type = rand() % 3 + StaticObjectFlowerBox1;
                            zone->m_objects.push_back(object);
                        }
                        if (k > lowest && k < highest) {
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(0, 255, 255)) {
                        if (style == PlacementSmithy) {
                            Zone::Object object;
                            object.type = StaticObjectFurnace;
                            object.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                            // Object layout: per-axis dimensions and placement offsets for this model.
                            object.rotation = -rotation;
                            object.size = plasma::Vector<3, float>(3.0f, 3.0f, 6.0f);
                            zone->m_objects.push_back(object);
                            if (k > lowest && k < highest) {
                                Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                                if (updates) {
                                    ServerUpdates::BlockPacket packet;
                                    packet.position = target;
                                    packet.block = air;
                                    updates->blocks.push_back(packet);
                                }
                                setBlock(target[0], target[1], target[2], air, zone);
                            }
                        } else if (style == PlacementCarpentersShop) {
                            Zone::Object object;
                            object.type = StaticObjectSaw;
                            object.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                            object.rotation = -rotation;
                            object.size = plasma::Vector<3, float>(1.5f, 1.5f, 1.0f);
                            zone->m_objects.push_back(object);
                            if (k > lowest && k < highest) {
                                Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                                if (updates) {
                                    ServerUpdates::BlockPacket packet;
                                    packet.position = target;
                                    packet.block = air;
                                    updates->blocks.push_back(packet);
                                }
                                setBlock(target[0], target[1], target[2], air, zone);
                            }
                        } else if (style == PlacementWeavingMill) {
                            Zone::Object object;
                            object.type = StaticObjectSpinningWheel;
                            object.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                            object.rotation = -rotation;
                            object.size = plasma::Vector<3, float>(1.5f, 1.5f, 1.0f);
                            zone->m_objects.push_back(object);
                            if (k > lowest && k < highest) {
                                Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                                if (updates) {
                                    ServerUpdates::BlockPacket packet;
                                    packet.position = target;
                                    packet.block = air;
                                    updates->blocks.push_back(packet);
                                }
                                setBlock(target[0], target[1], target[2], air, zone);
                            }
                        } else {
                            // A training target (an aiming dummy in style 10).
                            Spawn *dummy = new Spawn;
                            dummy->hostilityType = HostilityAttackableObject;
                            dummy->race = style == PlacementRangerHall ? RaceAim : RaceDummy;
                            dummy->rotation = (float)((2 - rotation) * 90);
                            dummy->position = plasma::Vector<3, Fixed>(target) +
                                              plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.5f, 0.5f, 0.5f));
                            dummy->level = levelAt(Fixed(target[0]), Fixed(target[1]));
                            zone->m_spawns.push_back(dummy);
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(0, 0, 127)) {
                        if (style != PlacementSmithy)
                            continue;
                        Zone::Object object;
                        object.type = StaticObjectCustomizationBench;
                        object.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                        // Object layout: per-axis dimensions and placement offsets for this model.
                        object.rotation = -rotation;
                        object.size = plasma::Vector<3, float>(1.8f, 1.8f, 1.2f);
                        zone->m_objects.push_back(object);
                        Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                        if (updates) {
                            ServerUpdates::BlockPacket packet;
                            packet.position = target;
                            packet.block = air;
                            updates->blocks.push_back(packet);
                        }
                        setBlock(target[0], target[1], target[2], air, zone);
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(0, 255, 127)) {
                        if (style == PlacementSmithy) {
                            Zone::Object object;
                            object.type = StaticObjectAnvil;
                            object.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                            object.rotation = -rotation;
                            object.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.0f);
                            zone->m_objects.push_back(object);
                            if (k > lowest && k < highest) {
                                Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                                if (updates) {
                                    ServerUpdates::BlockPacket packet;
                                    packet.position = target;
                                    packet.block = air;
                                    updates->blocks.push_back(packet);
                                }
                                setBlock(target[0], target[1], target[2], air, zone);
                            }
                        } else if (style == PlacementCarpentersShop) {
                            Zone::Object object;
                            object.type = StaticObjectWorkbench;
                            object.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                            object.rotation = -rotation;
                            object.size = plasma::Vector<3, float>(1.8f, 1.8f, 1.2f);
                            zone->m_objects.push_back(object);
                            if (k > lowest && k < highest) {
                                Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                                if (updates) {
                                    ServerUpdates::BlockPacket packet;
                                    packet.position = target;
                                    packet.block = air;
                                    updates->blocks.push_back(packet);
                                }
                                setBlock(target[0], target[1], target[2], air, zone);
                            }
                        } else if (style == PlacementWeavingMill) {
                            Zone::Object object;
                            object.type = StaticObjectLoom;
                            object.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                            object.rotation = -rotation;
                            object.size = plasma::Vector<3, float>(1.5f, 1.5f, 3.0f);
                            zone->m_objects.push_back(object);
                            if (k > lowest && k < highest) {
                                Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                                if (updates) {
                                    ServerUpdates::BlockPacket packet;
                                    packet.position = target;
                                    packet.block = air;
                                    updates->blocks.push_back(packet);
                                }
                                setBlock(target[0], target[1], target[2], air, zone);
                            }
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(0, 127, 0)) {
                        Zone::Object object;
                        object.type = rand() % StaticObjectVaseCount + StaticObjectVase1;
                        object.position = plasma::Vector<3, Fixed>(target) + plasma::Vector<3, Fixed>(0.5, 0.5, 0);
                        object.rotation = rand() % 4;
                        if (object.type < StaticObjectVase5)
                            object.size = plasma::Vector<3, float>(0.5f, 0.5f, 0.5f);
                        else
                            object.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.0f);
                        zone->m_objects.push_back(object);
                        if (k > lowest && k < highest) {
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                    if (color == plasma::Vector<3, unsigned char>(127, 0, 127)) {
                        // A resident: the style decides the class and, for styles 9..12, an outfit of level 20.
                        Spawn *resident = new Spawn;
                        resident->hostilityType = HostilityFriendlyNpc;
                        resident->race = rand() % (RaceHumanFemale - RaceElfMale + 1);
                        resident->rotation = (float)((2 - rotation) * 90);
                        resident->position = plasma::Vector<3, Fixed>(target) +
                                             plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.5f, 0.5f, 0.5f));
                        resident->equipment.chest.category = ItemCategoryChest;
                        resident->equipment.chest.id = 0;
                        resident->equipment.chest.material = ItemMaterialClothing;
                        resident->level = levelAt(Fixed(target[0]), Fixed(target[1]));
                        switch (style) {
                        case PlacementAnalyzer:
                            resident->classType = ClassAnalyzer;
                            break;
                        case PlacementSmithy:
                            resident->classType = ClassSmith;
                            break;
                        case PlacementCarpentersShop:
                            resident->classType = ClassCarpenter;
                            break;
                        case PlacementWeavingMill:
                            resident->classType = ClassTailor;
                            break;
                        case PlacementAdapter:
                            resident->level = AdapterResidentLevel;
                            resident->race = rand() % 2 + 2;
                            resident->classType = ClassAdapter;
                            break;
                        case PlacementWarriorHall:
                            resident->appearance.appearanceFlags = (1 << AppearanceFlagClassMaster);
                            resident->level = 20;
                            resident->race = RaceHumanMale;
                            resident->classType = ClassWarrior;
                            resident->equipment.weaponRight.category = ItemCategoryWeapon;
                            resident->equipment.weaponRight.id =
                                rand() % (WeaponGreatmace - WeaponGreatsword + 1) + WeaponGreatsword;
                            resident->equipment.weaponRight.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.weaponRight.level = (short)resident->level;
                            resident->equipment.weaponRight.material = ItemMaterialIron;
                            resident->equipment.shoulder.category = ItemCategoryShoulder;
                            resident->equipment.shoulder.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.shoulder.level = (short)resident->level;
                            resident->equipment.shoulder.material = ItemMaterialIron;
                            resident->equipment.chest.category = ItemCategoryChest;
                            resident->equipment.chest.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.chest.level = (short)resident->level;
                            resident->equipment.chest.material = ItemMaterialIron;
                            resident->equipment.feet.category = ItemCategoryFeet;
                            resident->equipment.feet.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.feet.level = (short)resident->level;
                            resident->equipment.feet.material = ItemMaterialIron;
                            resident->equipment.hands.category = ItemCategoryHands;
                            resident->equipment.hands.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.hands.level = (short)resident->level;
                            resident->equipment.hands.material = ItemMaterialIron;
                            break;
                        case PlacementRangerHall:
                            resident->appearance.appearanceFlags = (1 << AppearanceFlagClassMaster);
                            resident->level = 20;
                            resident->race = RaceHumanMale;
                            resident->classType = ClassRanger;
                            resident->equipment.weaponRight.category = ItemCategoryWeapon;
                            resident->equipment.weaponRight.id = rand() % 2 + WeaponBow;
                            resident->equipment.weaponRight.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.weaponRight.level = (short)resident->level;
                            resident->equipment.weaponRight.material = ItemMaterialWood;
                            resident->equipment.shoulder.category = ItemCategoryShoulder;
                            resident->equipment.shoulder.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.shoulder.level = (short)resident->level;
                            resident->equipment.shoulder.material = ItemMaterialLinen;
                            resident->equipment.chest.category = ItemCategoryChest;
                            resident->equipment.chest.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.chest.level = (short)resident->level;
                            resident->equipment.chest.material = ItemMaterialLinen;
                            resident->equipment.feet.category = ItemCategoryFeet;
                            resident->equipment.feet.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.feet.level = (short)resident->level;
                            resident->equipment.feet.material = ItemMaterialLinen;
                            resident->equipment.hands.category = ItemCategoryHands;
                            resident->equipment.hands.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.hands.level = (short)resident->level;
                            resident->equipment.hands.material = ItemMaterialLinen;
                            break;
                        case PlacementRogueHall:
                            resident->appearance.appearanceFlags = (1 << AppearanceFlagClassMaster);
                            resident->level = 20;
                            resident->race = RaceHumanMale;
                            resident->classType = ClassRogue;
                            resident->equipment.weaponRight.category = ItemCategoryWeapon;
                            resident->equipment.weaponRight.id = WeaponLongsword;
                            resident->equipment.weaponRight.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.weaponRight.level = (short)resident->level;
                            resident->equipment.weaponRight.material = ItemMaterialIron;
                            resident->equipment.shoulder.category = ItemCategoryShoulder;
                            resident->equipment.shoulder.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.shoulder.level = (short)resident->level;
                            resident->equipment.shoulder.material = ItemMaterialCotton;
                            resident->equipment.chest.category = ItemCategoryChest;
                            resident->equipment.chest.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.chest.level = (short)resident->level;
                            resident->equipment.chest.material = ItemMaterialCotton;
                            resident->equipment.feet.category = ItemCategoryFeet;
                            resident->equipment.feet.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.feet.level = (short)resident->level;
                            resident->equipment.feet.material = ItemMaterialCotton;
                            resident->equipment.hands.category = ItemCategoryHands;
                            resident->equipment.hands.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.hands.level = (short)resident->level;
                            resident->equipment.hands.material = ItemMaterialCotton;
                            break;
                        case PlacementMageHall:
                            resident->appearance.appearanceFlags = (1 << AppearanceFlagClassMaster);
                            resident->level = 20;
                            resident->race = RaceHumanMale;
                            resident->classType = ClassMage;
                            resident->equipment.weaponRight.category = ItemCategoryWeapon;
                            resident->equipment.weaponRight.id = WeaponStaff;
                            resident->equipment.weaponRight.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.weaponRight.level = (short)resident->level;
                            resident->equipment.weaponRight.material = ItemMaterialWood;
                            resident->equipment.shoulder.category = ItemCategoryShoulder;
                            resident->equipment.shoulder.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.shoulder.level = (short)resident->level;
                            resident->equipment.shoulder.material = ItemMaterialSilk;
                            resident->equipment.chest.category = ItemCategoryChest;
                            resident->equipment.chest.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.chest.level = (short)resident->level;
                            resident->equipment.chest.material = ItemMaterialSilk;
                            resident->equipment.feet.category = ItemCategoryFeet;
                            resident->equipment.feet.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.feet.level = (short)resident->level;
                            resident->equipment.feet.material = ItemMaterialSilk;
                            resident->equipment.hands.category = ItemCategoryHands;
                            resident->equipment.hands.rarity = rand() % (ItemRarityYellow + 1);
                            resident->equipment.hands.level = (short)resident->level;
                            resident->equipment.hands.material = ItemMaterialSilk;
                            break;
                        }
                        SequentialBehavior *behavior = new SequentialBehavior;
                        Behavior *combat = new CombatBehavior(20.0f);
                        behavior->m_behaviors.push_back(combat);
                        Behavior *look = new LookAtPlayerBehavior;
                        behavior->m_behaviors.push_back(look);
                        WalkPathBehavior *home = new WalkPathBehavior(2.0f);
                        home->m_path.push_back(resident->position);
                        Behavior *walk = home;
                        behavior->m_behaviors.push_back(walk);
                        resident->m_behavior = behavior;
                        zone->m_spawns.push_back(resident);
                        if (k > lowest && k < highest) {
                            Block air(plasma::Vector<3, unsigned char>(0, 0, 0), (1 << BlockFlagInterior));
                            if (updates) {
                                ServerUpdates::BlockPacket packet;
                                packet.position = target;
                                packet.block = air;
                                updates->blocks.push_back(packet);
                            }
                            setBlock(target[0], target[1], target[2], air, zone);
                        }
                        continue;
                    }
                }
                if (style == PlacementWater && color == plasma::Vector<3, unsigned char>(0, 0, 255)) {
                    // Water in style 8.
                    Block water(plasma::Vector<3, unsigned char>(255, 255, 255), BlockWater);
                    if (updates) {
                        ServerUpdates::BlockPacket packet;
                        packet.position = target;
                        packet.block = water;
                        updates->blocks.push_back(packet);
                    }
                    setBlock(target[0], target[1], target[2], water, zone);
                } else {
                    Block block(color, (unsigned char)(blockType | (1 << BlockFlagInterior)));
                    if (updates) {
                        ServerUpdates::BlockPacket packet;
                        packet.position = target;
                        packet.block = block;
                        updates->blocks.push_back(packet);
                    }
                    setBlock(target[0], target[1], target[2], block, zone);
                }
            }
        }
    }
}

// Settles an object on the ground at its position: lowers it while its footprint (its size, turned
// by an odd rotation) is free, then raises it while the footprint is blocked, 50 blocks at most
// each way. Fails at or below sea level, where the whole footprint must rest on solid blocks and
// does not, and in water.
bool World::settleObject(Zone::ObjectState &object, Zone *zone, bool needsFloor)
{
    plasma::Vector<2, float> size(object.size[0], object.size[1]);
    if (object.rotation % 2 != 0)
        std::swap(size[0], size[1]);
    Fixed halfX(size[0] * 0.5f);
    int x0 = (int)(object.position[0] - halfX);
    int x1 = (int)(object.position[0] + halfX);
    Fixed halfY(size[1] * 0.5f);
    int y0 = (int)(object.position[1] - halfY);
    int y1 = (int)(object.position[1] + halfY);
    // Down while nothing under the footprint is solid.
    for (int i = 0; i < 50; i++) {
        int z = (int)object.position[2];
        for (int x = x0; x <= x1; x++) {
            for (int y = y0; y <= y1; y++) {
                if (getBlock(x, y, z, zone).isSolid())
                    goto grounded;
            }
        }
        object.position[2] -= Fixed(1);
    }
grounded:
    // Up while something in the footprint is solid.
    for (int i = 0; i < 50; i++) {
        int z = (int)object.position[2];
        bool blocked = false;
        for (int x = x0; x <= x1; x++) {
            for (int y = y0; y <= y1; y++) {
                if (getBlock(x, y, z, zone).isSolid()) {
                    blocked = true;
                    goto checked;
                }
            }
        }
    checked:
        if (!blocked)
            break;
        object.position[2] += Fixed(1);
    }
    if (object.position[2].raw <= 0)
        return false;
    if (needsFloor) {
        int z = (int)(object.position[2] - Fixed(1));
        for (int x = x0; x <= x1; x++) {
            for (int y = y0; y <= y1; y++) {
                if (!getBlock(x, y, z, zone).isSolid())
                    return false;
            }
        }
    }
    if ((getBlock(object.position[0], object.position[1], object.position[2], zone).type & BlockTypeMask) == BlockWater)
        return false;
    return true;
}

namespace {
enum {
    ArmorModifierRange = 100
};
}

Item World::randomArmor(int level, int rarity, int classType)
{
    std::vector<Item> items;
    Item item;
    item.level = level;
    item.rarity = rarity;
    if (classType == ClassWarrior || classType < 0) {
        item.material = ItemMaterialIron;
        item.category = ItemCategoryShoulder;
        item.modifier = rand() % ArmorModifierRange;
        items.push_back(item);
        item.category = ItemCategoryChest;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryHands;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryFeet;
        item.modifier = rand() % 100;
        items.push_back(item);
    }
    if (classType == ClassMage || classType < 0) {
        item.material = ItemMaterialSilk;
        item.category = ItemCategoryShoulder;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryShoulder;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryChest;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryHands;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryFeet;
        item.modifier = rand() % 100;
        items.push_back(item);
    }
    if (classType == ClassRanger || classType < 0) {
        item.material = ItemMaterialLinen;
        item.category = ItemCategoryShoulder;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryShoulder;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryChest;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryHands;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryFeet;
        item.modifier = rand() % 100;
        items.push_back(item);
    }
    if (classType == ClassRogue || classType < 0) {
        item.material = ItemMaterialCotton;
        item.category = ItemCategoryShoulder;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryShoulder;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryChest;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryHands;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.category = ItemCategoryFeet;
        item.modifier = rand() % 100;
        items.push_back(item);
    }
    item.material = rand() % 2 == 0 ? ItemMaterialGold : ItemMaterialSilver;
    item.category = ItemCategoryNeck;
    item.modifier = rand() % 100;
    items.push_back(item);
    item.material = rand() % 2 == 0 ? ItemMaterialGold : ItemMaterialSilver;
    item.category = ItemCategoryRing;
    item.modifier = rand() % 100;
    items.push_back(item);
    return items[rand() % items.size()];
}

// The candidates follow the terrain: blendedTemperature acts as the temperature (snow fauna below
// 0.2, desert fauna from 0.8 where blendedHumidity, the humidity, is below 0.2, jungle fauna where
// both reach 0.6) and blendedFirelands as the fire lands (fire creatures above 0.1). Sea-level
// blocks (z below 4) get shore creatures. A fly when no candidate applies.
int World::randomCreatureRace(int x, int y, int z, bool humanoids)
{
    float temperature = blendedTemperature(x, y);
    float humidity = blendedHumidity(x, y);
    float firelands = blendedFirelands(x, y);
    float vegetation = vegetationDensity(x, y, 0);
    std::vector<int> races;
    if (z < 0) {
        races.push_back(RaceSapphireFish);
        races.push_back(RaceLemonFish);
        races.push_back(RaceSeahorse);
        races.push_back(RaceShark);
        races.push_back(RaceLanternFish);
        races.push_back(RaceMawFish);
        races.push_back(RaceBlowfish);
        races.push_back(RacePiranha);
    } else if (humanoids) {
        races.push_back(RaceHumanMale);
        races.push_back(RaceGoblinMale);
        races.push_back(RaceLizardmanMale);
        races.push_back(RaceDwarfMale);
        races.push_back(RaceOrcMale);
        races.push_back(RaceUndeadMale);
        races.push_back(RaceGnobold);
        races.push_back(RaceGnoll);
        races.push_back(RaceSpikeCreature);
    } else if (firelands > 0.1f) {
        races.push_back(RaceDevourer);
        races.push_back(RaceMinotaur);
        races.push_back(RaceImp);
        races.push_back(RaceEmberGolem);
        races.push_back(RaceFireBeetle);
        races.push_back(RaceMosquito);
        races.push_back(RaceHellDemon);
    } else {
        races.push_back(RaceSkullBull);
        races.push_back(RaceOgre);
        if (vegetation > 0.3f)
            races.push_back(RaceRockling);
        if (z < 4) {
            races.push_back(RaceCrab);
            races.push_back(RaceSeagull);
            races.push_back(RaceSpitter);
            races.push_back(RaceTurtle);
        } else if (temperature >= 0.2f && temperature < 0.8f) {
            races.push_back(RaceInsectGuard);
            races.push_back(RaceDuckbill);
            races.push_back(RaceWolf);
            races.push_back(RaceChicken);
            races.push_back(RaceTurtle);
            races.push_back(RaceHornet);
            races.push_back(RaceCrow);
            races.push_back(RaceFly);
            races.push_back(RacePlainRunner);
            races.push_back(RacePeacock);
            races.push_back(RacePlantCreature);
            races.push_back(RaceRadishCreature);
            races.push_back(RaceOnionling);
            races.push_back(RaceMole);
            races.push_back(RaceBiter);
            races.push_back(RaceCow);
            races.push_back(RaceSquirrel);
            races.push_back(RaceRaccoon);
            races.push_back(RaceOwl);
            races.push_back(RaceBunny);
            races.push_back(RaceBarkBeetle);
            races.push_back(RaceSnoutBeetle);
            races.push_back(RaceLemonBeetle);
            races.push_back(RaceSheep);
            races.push_back(RacePig);
            races.push_back(RaceCat);
            races.push_back(RaceBrownCat);
            races.push_back(RaceWhiteCat);
            races.push_back(RaceCollie);
            races.push_back(RaceShepherdDog);
            races.push_back(RaceTerrier);
            races.push_back(RaceScottishTerrier);
            races.push_back(RaceHorse);
            races.push_back(RaceAlpaca);
            races.push_back(RaceBrownAlpaca);
            races.push_back(RaceBumblebee);
        } else if (temperature >= 0.8f && humidity < 0.2f) {
            races.push_back(RaceInsectGuard);
            races.push_back(RaceMidge);
            races.push_back(RaceDesertOnionling);
            races.push_back(RaceSandHorror);
            races.push_back(RaceDevourer);
            races.push_back(RaceDesertRunner);
            races.push_back(RaceNomadMale);
            races.push_back(RaceNomadFemale);
            races.push_back(RaceBarkBeetle);
            races.push_back(RaceFireBeetle);
            races.push_back(RaceBiter);
            races.push_back(RaceMosquito);
            races.push_back(RacePorcupine);
            races.push_back(RaceCamel);
        } else if (temperature >= 0.6f && humidity >= 0.6f) {
            races.push_back(RaceInsectGuard);
            races.push_back(RaceKoala);
            races.push_back(RaceCrocodile);
            races.push_back(RaceDuckbill);
            races.push_back(RaceOnionling);
            races.push_back(RaceLeafRunner);
            races.push_back(RaceFrog);
            races.push_back(RaceFly);
            races.push_back(RaceParrot);
            races.push_back(RacePeacock);
            races.push_back(RaceNomadMale);
            races.push_back(RaceNomadFemale);
            races.push_back(RaceTurtle);
            races.push_back(RaceBarkBeetle);
            races.push_back(RaceSnoutBeetle);
            races.push_back(RaceLemonBeetle);
            races.push_back(RaceBiter);
            races.push_back(RaceMosquito);
            races.push_back(RacePorcupine);
            races.push_back(RaceMonkey);
            races.push_back(RaceAlpaca);
            races.push_back(RaceBrownAlpaca);
        } else if (temperature < 0.2f) {
            races.push_back(RaceWolf);
            races.push_back(RaceChicken);
            races.push_back(RaceSnowRunner);
            races.push_back(RaceSquirrel);
            races.push_back(RacePenguin);
            races.push_back(RaceSkullBull);
            races.push_back(RacePolarGnoll);
            races.push_back(RaceAlpaca);
            races.push_back(RaceBrownAlpaca);
            races.push_back(RaceBunny);
        }
    }
    if (races.empty())
        return RaceFly;
    return races[rand() % races.size()];
}

Item World::randomFormula(int level, int rarity)
{
    Item item;
    if (rand() % 2)
        item = randomWeapon(level, rarity, -1);
    else
        item = randomArmor(level, rarity, -1);
    item.formulaCategory = item.category;
    item.category = ItemCategoryFormula;
    return item;
}

namespace {
enum {
    WallChestChanceDenominator = 50
};
}

// An object against a dungeon wall facing the given way: rarely a chest, else shelves, a table or
// a stool of the dungeon variant's kind, moved a little off the wall.
Zone::Object World::makeWallObject(const plasma::Vector<3, Fixed> &position, int rotation, int variant)
{
    Zone::Object object;
    object.position = position;
    object.rotation = rotation;
    if (rand() % WallChestChanceDenominator == 0) {
        // Object layout: per-axis dimensions and placement offsets for this model.
        object.type = StaticObjectChest;
        object.size = plasma::Vector<3, float>(1.5f, 1.0f, 1.0f);
        object.rotation = (rotation + 2) % 4;
        switch (rotation) {
        case 0:
            object.position[1] += Fixed(-0.5);
            break;
        case 1:
            object.position[0] += Fixed(0.5);
            break;
        case 2:
            object.position[1] += Fixed(0.5);
            break;
        case 3:
            object.position[0] += Fixed(-0.5);
            break;
        }
        return object;
    }
    switch (rand() % 3) {
    case 0:
        // Shelves: three kinds per variant.
        if (variant == Region::Record::DungeonPalace)
            object.type = rand() % 3 + StaticObjectShelf1;
        else if (variant == Region::Record::DungeonTemple)
            object.type = rand() % 3 + StaticObjectStoneShelf1;
        else if (variant == Region::Record::DungeonPyramid)
            object.type = rand() % 3 + StaticObjectSandstoneShelf1;
        else
            object.type = rand() % 3 + StaticObjectCastleShelf1;
        object.size = plasma::Vector<3, float>(2.0f, 1.0f, 1.57f);
        break;
    case 1:
        // A table, one to two blocks off the wall.
        if (variant == Region::Record::DungeonTemple)
            object.type = StaticObjectStoneTable;
        else
            object.type = variant == Region::Record::DungeonPyramid ? StaticObjectTable14 : StaticObjectTable;
        object.size = plasma::Vector<3, float>(3.0f, 3.0f, 1.0f);
        switch (rotation) {
        case 0:
            object.position[1] -= Fixed(rand() / float(RAND_MAX) + 1.0f);
            break;
        case 1:
            object.position[0] += Fixed(rand() / float(RAND_MAX) + 1.0f);
            break;
        case 2:
            object.position[1] += Fixed(rand() / float(RAND_MAX) + 1.0f);
            break;
        case 3:
            object.position[0] -= Fixed(rand() / float(RAND_MAX) + 1.0f);
            break;
        }
        break;
    case 2:
        // A stool, up to a block off the wall.
        if (variant == Region::Record::DungeonTemple)
            object.type = StaticObjectStoneStool;
        else
            object.type = variant == Region::Record::DungeonPyramid ? StaticObjectSandstoneStool : StaticObjectStool;
        object.size = plasma::Vector<3, float>(1.0f, 1.0f, 0.5f);
        switch (rotation) {
        case 0:
            object.position[1] -= Fixed(rand() / float(RAND_MAX));
            break;
        case 1:
            object.position[0] += Fixed(rand() / float(RAND_MAX));
            break;
        case 2:
            object.position[1] += Fixed(rand() / float(RAND_MAX));
            break;
        case 3:
            object.position[0] -= Fixed(rand() / float(RAND_MAX));
            break;
        }
        break;
    }
    return object;
}

int World::randomHostileRace(int x, int y, int level)
{
    std::vector<int> races;
    races.push_back(RaceGnoll);
    races.push_back(RaceGnobold);
    races.push_back(RaceZombie);
    races.push_back(RaceSpectrino);
    races.push_back(RaceSpikeCreature);
    races.push_back(RaceBlueSlime);
    races.push_back(RaceWizard);
    races.push_back(RaceWitch);
    races.push_back(RaceInsectoid);
    return races[rand() % races.size()];
}

namespace {
enum {
    ManaCubeModifierRange = 200
};
}

Item World::randomManaCube(int level, int rarity)
{
    Item item;
    item.category = ItemCategoryManaCube;
    item.modifier = rand() % ManaCubeModifierRange;
    return item;
}

namespace {
enum {
    CyclopsUnlockLevel = 10
};
enum {
    LichUnlockLevel = 20
};
enum {
    GolemUnlockLevel = 50
};
enum {
    HellDemonUnlockLevel = 70
};
}

int World::randomMonsterRace(int x, int y, int level)
{
    RegionRecord *region = getRegionRecord(x, y);
    if (!region)
        return RaceTroll;
    std::vector<int> races;
    if (region->temperature > 0.2f)
        races.push_back(RaceTroll);
    else
        races.push_back(RaceYeti);
    if (region->temperature > 0.2f)
        races.push_back(RaceSaurian);
    else
        races.push_back(RaceMammoth);
    if (level > CyclopsUnlockLevel)
        races.push_back(RaceCyclops);
    if (level > LichUnlockLevel)
        races.push_back(RaceLich);
    if (level > GolemUnlockLevel) {
        if (region->temperature < 0.2f)
            races.push_back(RaceSnowGolem);
        else if (region->firelands == true)
            races.push_back(RaceEmberGolem);
        else
            races.push_back(RaceGolem);
        races.push_back(RaceDarkTroll);
    }
    if (level > HellDemonUnlockLevel)
        races.push_back(RaceHellDemon);
    return races[rand() % races.size()];
}

Item World::randomPetFood(int level, int rarity)
{
    Item item;
    item.category = ItemCategoryPetFood;
    // An empty pet-food list retains  division by zero.
    item.id = m_petFoodRaces[rand() % m_petFoodRaces.size()];
    return item;
}

// A weapon or a piece of armour of any class (each at its own random rarity), or a formula.
Item World::randomItem(int level, int rarity)
{
    std::vector<Item> items;
    Item item;
    item.level = level;
    // Drawn and then replaced by the first candidate's own draw.
    item.rarity = randomRarity(rarity, false);
    item.material = ItemMaterialIron;
    item.category = ItemCategoryWeapon;
    item.id = rand() % (WeaponMace - WeaponSword + 1);
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    // This candidate is overwritten before it is appended.
    item.id = rand() % (WeaponGreatmace - WeaponGreatsword + 1) + WeaponGreatsword;
    item.rarity = randomRarity(rarity, false);
    item.id = WeaponShield;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.material = ItemMaterialWood;
    item.id = WeaponBow;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.id = WeaponCrossbow;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.id = WeaponStaff;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.id = WeaponWand;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryShoulder;
    item.id = WeaponSword;
    item.material = ItemMaterialIron;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryChest;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryHands;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryFeet;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.material = ItemMaterialSilk;
    item.category = ItemCategoryShoulder;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryShoulder;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryChest;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryHands;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryFeet;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.material = ItemMaterialLinen;
    item.category = ItemCategoryShoulder;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryShoulder;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryChest;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryHands;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryFeet;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.material = ItemMaterialCotton;
    item.category = ItemCategoryShoulder;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryShoulder;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryChest;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryHands;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    item.category = ItemCategoryFeet;
    item.rarity = randomRarity(rarity, false);
    items.push_back(item);
    items.push_back(randomFormula(level, rarity));
    return items[rand() % items.size()];
}

int World::randomRarity(int rarity, bool increase)
{
    int result = rand() % (rarity + 1);
    if (rand() % RarityFirstUpgradeChanceDenominator == 0)
        ++result;
    if (rand() % RaritySecondUpgradeChanceDenominator == 0)
        ++result;
    if (rand() % RarityThirdUpgradeChanceDenominator == 0)
        ++result;
    if (increase)
        result = rarity + 1;
    if (result > ItemRarityYellow)
        result = ItemRarityYellow;
    return result;
}

int World::randomRaceVariant(int x, int y, int level, int race)
{
    switch (race) {
    case RaceOrcMale:
    case RaceOrcFemale:
        return rand() % 2 + RaceOrcMale;
    case RaceGoblinMale:
    case RaceGoblinFemale:
        return rand() % 2 + RaceGoblinMale;
    case RaceDwarfMale:
    case RaceDwarfFemale:
        return rand() % 2 + RaceDwarfMale;
    case RaceHumanMale:
    case RaceHumanFemale:
        return rand() % 2 + RaceHumanMale;
    case RaceElfMale:
    case RaceElfFemale:
        return RaceElfMale + rand() % 2;
    case RaceLizardmanMale:
    case RaceLizardmanFemale:
        return rand() % 2 + RaceLizardmanMale;
    case RaceUndeadMale:
    case RaceUndeadFemale:
        return rand() % 2 + RaceUndeadMale;
    case RaceFrogmanMale:
    case RaceFrogmanFemale:
        return rand() % 2 + RaceFrogmanMale;
    case RaceAlpaca:
    case RaceBrownAlpaca:
        return rand() % 2 + RaceAlpaca;
    case RaceNomadMale:
    case RaceNomadFemale:
        return rand() % 2 + RaceNomadMale;
    case RaceGreenSlime:
    case RacePinkSlime:
    case RaceYellowSlime:
    case RaceBlueSlime:
        return rand() % 4 + RaceGreenSlime;
    default:
        return race;
    }
}

namespace {
enum {
    InventoryCommonLootChanceDenominator = 6
};
enum {
    InventoryIngredientChoiceCount = 5
};
}

Item randomInventoryItem(int level, int rarity)
{
    Item item;
    if (rand() % 2) {
        float amount = (float)pow(2.0, (double)(level * 0.25f * (1.0f + 2.0f * rand() / RAND_MAX)));
        item.level = rand() % (2 * (int)amount + 2);
        item.category = ItemCategoryCoin;
        item.material = ItemMaterialCopper;
        return item;
    } else {
        item.level = level;
        if (rand() % InventoryCommonLootChanceDenominator) {
            // The second case is unreachable for rand % 2 in the code.
            switch (rand() % 2) {
            case 1:
                item.category = ItemCategoryCandle;
                item.modifier = rand() % 3;
                item.material = 0;
                break;
            case 2:
                item.level = 1;
                item.category = ItemCategoryMaterial;
                item.id = MaterialItemFlask;
                item.material = ItemMaterialGlass;
                break;
            }
        } else {
            switch (rand() % InventoryIngredientChoiceCount) {
            case 0:
                item.level = level;
                item.category = ItemCategoryConsumable;
                item.id = ConsumableLifePotion;
                break;
            case 1:
                item.category = ItemCategoryConsumable;
                item.id = ConsumableGinsengSoup;
                item.level = level;
                // The soup case falls through to the silver-currency case.
            case 2: {
                float amount = (float)pow(2.0, (double)(level * 0.25f * (1.0f + 2.0f * rand() / RAND_MAX)));
                item.level = rand() % (2 * (int)amount + 2);
                item.category = ItemCategoryCoin;
                item.material = ItemMaterialSilver;
                break;
            }
            case 3:
                item.level = level;
                item.category = ItemCategoryConsumable;
                item.id = ConsumableBomb;
                break;
            case 4:
                item.category = ItemCategoryConsumable;
                item.id = ConsumableSnowBerryMash;
                item.level = level;
                break;
            }
        }
    }
    return item;
}

namespace {
enum {
    TorchVariantCount = 4
};
enum {
    RandomArmorCategoryChoiceCount = 3
};
}

// The torch on a dungeon wall for a variant: a plain or lit object plant (lit ones give light,
// PlantFlagEmissive, in their colour).
Zone::Plant World::makeTorch(int variant, const plasma::Vector<3, Fixed> &position, float rotation)
{
    Zone::Plant torch;
    torch.position = position;
    torch.rotation = rotation;
    torch.scale = 0.0625f;
    switch (variant) {
    case Region::Record::DungeonTemple:
        if (rand() % 2) {
            torch.type = rand() % TorchVariantCount + PlantIncaArt1;
            break;
        }
        // Otherwise the lit torch of variant 5.
    case Region::Record::DungeonPyramid:
        torch.type = PlantTorchYellow;
        // Procedural RGB palette and brightness coefficients for this surface.
        torch.flags = (1 << PlantFlagEmissive);
        torch.color = plasma::Vector<3, float>(0.8f, 0.7f, 0.2f);
        break;
    case Region::Record::DungeonPalace:
        torch.type = PlantCrest1;
        break;
    case Region::Record::DungeonCastle:
        switch (rand() % 2) {
        case 0:
            torch.type = PlantCastleChain;
            torch.position[2] += Fixed(2);
            break;
        case 1:
            torch.type = PlantTorchYellow;
            torch.flags = (1 << PlantFlagEmissive);
            torch.color = plasma::Vector<3, float>(0.8f, 0.7f, 0.2f);
            break;
        }
        break;
    case Region::Record::DungeonRuins:
    case Region::Record::DungeonCatacombs:
        switch (rand() % RandomArmorCategoryChoiceCount) {
        case 0:
            torch.type = PlantCastleChain;
            break;
        case 1:
            torch.type = PlantWallSkull;
            break;
        case 2:
            torch.type = PlantTorchGreen;
            torch.flags = (1 << PlantFlagEmissive);
            torch.color = plasma::Vector<3, float>(0.0f, 0.5f, 0.1f);
            break;
        }
        break;
    }
    return torch;
}

namespace {
enum {
    WeaponModifierRange = 100
};
}

Item World::randomWeapon(int level, int rarity, int classType)
{
    std::vector<Item> items;
    Item item;
    item.level = level;
    item.rarity = rarity;
    item.material = ItemMaterialIron;
    item.category = ItemCategoryWeapon;
    if (classType == ClassWarrior || classType < 0) {
        item.id = rand() % (WeaponMace - WeaponSword + 1);
        item.modifier = rand() % WeaponModifierRange;
        items.push_back(item);
        item.material = ItemMaterialIron;
        item.id = rand() % (WeaponGreatmace - WeaponGreatsword + 1) + WeaponGreatsword;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.id = WeaponShield;
        item.modifier = rand() % 100;
        items.push_back(item);
    }
    if (classType == ClassRogue || classType < 0) {
        item.id = WeaponDagger;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.id = WeaponLongsword;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.id = WeaponFist;
        item.modifier = rand() % 100;
        items.push_back(item);
    }
    item.material = ItemMaterialWood;
    if (classType == ClassRanger || classType < 0) {
        item.id = WeaponBow;
        item.modifier = rand() % 100;
        items.push_back(item);
        // The crossbow candidate is overwritten before it is appended.
        item.id = WeaponCrossbow;
        item.modifier = rand() % 100;
        item.id = WeaponBoomerang;
        item.modifier = rand() % 100;
        items.push_back(item);
    }
    if (classType == ClassMage || classType < 0) {
        item.id = WeaponStaff;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.id = WeaponWand;
        item.modifier = rand() % 100;
        items.push_back(item);
        item.id = WeaponBracelet;
        item.material = rand() % 2 == 0 ? ItemMaterialSilver : ItemMaterialGold;
        item.modifier = rand() % 100;
        items.push_back(item);
    }
    // A nonnegative, unsupported class leaves the candidate list empty in the code.
    return items[rand() % items.size()];
}

// The squared distance from the record's centre relative to its radius (1 on the rim). Records of
// kinds 11, 12 and 14 are round; the others' shapes are warped by noise, 100 blocks deep for kind
// 13 and 256 blocks for the rest. 0 for a record without a radius.
float Region::Record::shapeDistance(const Fixed &x, const Fixed &y)
{
    float radius = this->radius;
    if (radius < 0.001f)
        return 0.0f;
    if (kind == Region::Record::SceneryRock || kind == Region::Record::SceneryTree ||
        kind == Region::Record::SceneryCastle) {
        float dx = x - center[0];
        float dy = y - center[1];
        return (dx * dx + dy * dy) / (radius * radius);
    }
    // Scenery outline warp noise channel offsets/frequency.
    if (kind == Region::Record::SceneryPeak) {
        float dx =
            x +
            Fixed(noise(toBlocks(Fixed(8432984) - scaled(x, -0.0025)), toBlocks(Fixed(90493) - scaled(y, -0.0025))) *
                  100.0f) -
            center[0];
        float dy = y +
                   Fixed(noise(toBlocks(Fixed(3423) - scaled(x, -0.0025)), toBlocks(Fixed(112) - scaled(y, -0.0025))) *
                         100.0f) -
                   center[1];
        return (dx * dx + dy * dy) / (radius * radius);
    }
    float dx = x +
               Fixed(noise(toBlocks(Fixed(8432984) - scaled(x, -0.0025)), toBlocks(Fixed(90493) - scaled(y, -0.0025))) *
                     256.0f) -
               center[0];
    float dy =
        y +
        Fixed(noise(toBlocks(Fixed(3423) - scaled(x, -0.0025)), toBlocks(Fixed(112) - scaled(y, -0.0025))) * 256.0f) -
        center[1];
    return (dx * dx + dy * dy) / (radius * radius);
}

// The river field combines two coordinate-warped noise channels scaled by a third. Roads, certain region kinds and the column firelands parameter raise it. The generator digs where 1 - 50 * value is positive.
float World::riverDistance(int x, int y, Zone *zone)
// River field noise frequencies, width modulation and road-network suppression.
{
    float river = (float)fabs(noise(m_riverNoiseOffsets[2] + x * 0.01, m_riverNoiseOffsets[3] + y * 0.01) * 0.1f +
                              noise(m_riverNoiseOffsets[0] + x * 0.001, m_riverNoiseOffsets[1] + y * 0.001));
    river *= (noise(x * 0.001, y * 0.001) + 1.0f) * 0.1f + 0.8f;
    float road = 1.0f - roadNetworkDistance(x, y) * 0.75f;
    if (road > 0.0f)
        river += road * road * 0.05f;
    Region::Record *record = getRecord(x / BlocksPerSceneryRecord, y / BlocksPerSceneryRecord);
    if (record) {
        int kind = record->kind;
        if (kind == Region::Record::SceneryCity || kind == Region::Record::SceneryMountain ||
            kind == Region::Record::SceneryLake || kind == Region::Record::SceneryPeak) {
            float inside = 1.0f - record->shapeDistance(Fixed(x), Fixed(y));
            river += 0.0f >= inside ? 0.0f : inside * inside;
        }
        if (kind == Region::Record::SceneryCanyon || kind == Region::Record::SceneryValley) {
            float inside = 1.0f - record->shapeDistance(Fixed(x), Fixed(y));
            float square = 0.0f >= inside ? 0.0f : inside * inside;
            river += square * 0.5f;
        }
    }
    Field *field = getField(x, y, zone);
    return (field ? field->firelands : blendedFirelands(x, y)) + river;
}

// Rock color combines gray stone and vertical noise shading, shifting blue in cold terrain, red in heat and burnt orange in firelands.
plasma::Vector<3, float> World::rockColor(int x, int y, int z, Zone *zone)
// Rock palette noise parameters.
{
    float shade = ((noise(y * 0.1 + 98984.0, z * 0.4) + noise(x * 0.1, z * 0.4)) * 0.5f + 1.0f) * 0.5f;
    float temperature = terrainTemperature(x, y, zone);
    // Rock climate/firelands palette.
    terrainHumidity(x, y, zone); // the code reads it and discards it
    plasma::Vector<3, float> stone(120.0f, 120.0f, 130.0f);
    if (temperature < 0.5f) {
        // Procedural RGB palette and brightness coefficients for this surface.
        float t = temperature * 2.0f;
        // Hot-rock palette transition temperature cutoff.
        stone = plasma::Vector<3, float>(10.0f, 20.0f, 50.0f) * (1.0f - t) +
                plasma::Vector<3, float>(160.0f, 160.0f, 170.0f) * t;
        // Hot-rock palette temperature cutoff and blend slope.
    } else if (temperature > 0.75f) {
        float t = (temperature - 0.75f) * 4.0f;
        float green = noise(t * 0.001 + 6544.0, y * 0.001 + 123.0) * 20.0f + 130.0f;
        stone = plasma::Vector<3, float>(120.0f, 120.0f, 130.0f) * (1.0f - t) +
                plasma::Vector<3, float>(200.0f, green, 100.0f) * t;
    }
    float blue = noise(m_rockColorNoiseOffsets[4] + x * 0.005, m_rockColorNoiseOffsets[5] + y * 0.005) + 1.0f;
    plasma::Vector<3, float> color = clampColor(plasma::Vector<3, float>(shade, shade, shade) * 160.0f + stone +
                                                plasma::Vector<3, float>(0.0f, 0.0f, blue) * 10.0f);
    Field *field = getField(x, y, zone);
    float firelands = field ? field->firelands : blendedFirelands(x, y);
    if (firelands > 0.0f) {
        float patches = noise(x * 0.05, y * 0.05) * 0.1f + noise(x * 0.02, y * 0.02);
        if (patches > 1.0f)
            patches = 1.0f;
        float bare = 1.0f - (float)fabs(patches);
        float density = bare * bare * bare * bare * bare;
        color = plasma::Vector<3, float>(density * 200.0f + 10.0f,
                                         (noise(x * 0.1, y * 0.1) + 1.0f) * density * 50.0f + 10.0f, 10.0f) *
                    firelands +
                color * (1.0f - firelands);
    }
    return color;
}

plasma::Vector<3, float> World::sandColor(int x, int y)
// Sand noise frequencies, channel offsets and green amplitude.
{
    float light = (noise(m_sandColorNoiseOffsets[0] + x * 0.03, m_sandColorNoiseOffsets[1] + y * 0.03) + 1.0f) * 0.5f;
    // Procedural RGB palette and brightness coefficients for this surface.
    float blend = (noise(m_sandColorNoiseOffsets[2] + x * 0.003, m_sandColorNoiseOffsets[3] + y * 0.003) + 1.0f) * 0.5f;
    return clampColor(
        plasma::Vector<3, float>(255.0f, noise(x * 0.01 + 493.0, y * 0.01 + 789.0) * 40.0f + 200.0f, 100.0f) *
            (1.0f - blend) +
        plasma::Vector<3, float>(255.0f, 150.0f, 50.0f) * blend +
        plasma::Vector<3, float>(light, light, light) * 80.0f);
}

int Dungeon::sizeX() const
{
    return m_rotation % 2 ? m_sizeY : m_sizeX;
}

int Dungeon::sizeY() const
{
    return m_rotation % 2 ? m_sizeX : m_sizeY;
}

int Dungeon::sizeZ() const
{
    return m_sizeZ;
}

plasma::Vector<3, float> World::snowColor(int x, int y)
// Snow palette blend noise frequency.
{
    // Procedural RGB palette and brightness coefficients for this surface.
    float blend = (noise(x * 0.04, y * 0.04) + 1.0f) * 0.5f;
    return clampColor(plasma::Vector<3, float>(190.0f, 220.0f, 255.0f) * blend +
                      plasma::Vector<3, float>(100.0f, 180.0f, 255.0f) * (1.0f - blend));
}

// Coordinates in the rotated dungeon become coordinates in its stored cells. The paved-road field
// at a block column (the zone generator paves where 1 - 50 * value > 0): the distance to the road
// network, cut down near a record's centre along two noisy directions, faded out at its very
// centre, and raised inside records of kinds 2 and 4.
float World::roadDistance(int x, int y, Zone *zone)
{
    float influence = recordInfluence(x, y);
    plasma::Vector<2, double> direction = jitter(x, y);
    float distance = roadNetworkDistance(x, y);
    if (influence > 0.0f) {
        float t = influence * 3.0f;
        if (t > 1.0f)
            t = 1.0f;
        float weight = 1.0f - (1.0f - t * t) * (1.0f - t * t);
        float a = (float)(cos(direction[0] * 360.0) * weight + 1.0);
        if (distance > a)
            distance = a;
        float b = (float)(cos(direction[1] * 360.0) * weight + 1.0);
        if (distance > b)
            distance = b;
    }
    if (influence > 0.65f) {
        double fade = (0.7 - influence) / 0.05f;
        if (!(fade > 0.0))
            fade = 0.0;
        distance *= (float)fade;
    }
    Region::Record *record = getRecord(x / BlocksPerSceneryRecord, y / BlocksPerSceneryRecord);
    if (record && (record->kind == Region::Record::SceneryMountain || record->kind == Region::Record::SceneryLake)) {
        float inside = 1.0f - record->shapeDistance(Fixed(x), Fixed(y));
        float square = 0.0f >= inside ? 0.0f : inside * inside;
        distance += square * 2.0f;
    }
    return distance;
}

plasma::Vector<3, float> World::pavingColor(int x, int y, int z)
// Paving color noise frequencies, offsets and amplitudes.
{
    float shade = ((noise(y * 0.01 + 98984.0, z * 0.3 + 8437.0) + noise(x * 0.01, z * 0.3)) * 0.5f + 1.0f) * 0.5f;
    float red = noise(m_pavingColorNoiseOffsets[0] + x * 0.005, m_pavingColorNoiseOffsets[1] + y * 0.005) * 20.0f;
    float green = noise(m_pavingColorNoiseOffsets[2] + x * 0.005, m_pavingColorNoiseOffsets[3] + y * 0.005) * 20.0f;
    // Procedural RGB palette and brightness coefficients for this surface.
    float blue = noise(m_pavingColorNoiseOffsets[4] + x * 0.005, m_pavingColorNoiseOffsets[5] + y * 0.005) * 20.0f;
    return clampColor(
        plasma::Vector<3, float>(red, green, blue) +
        (plasma::Vector<3, float>(200.0f, 200.0f, 200.0f) * shade + plasma::Vector<3, float>(50.0f, 50.0f, 50.0f)));
}

void Dungeon::transform(int &x, int &y, int &z) const
{
    switch (m_rotation % 4) {
    case 1:
        std::swap(x, y);
        x = m_sizeX - x - 1;
        break;
    case 2:
        x = m_sizeX - x - 1;
        y = m_sizeY - y - 1;
        break;
    case 3:
        std::swap(x, y);
        y = m_sizeY - y - 1;
        break;
    }
    if (m_reflectY)
        y = m_sizeY - y - 1;
}

void Dungeon::inverseTransform(int &x, int &y, int &z) const
{
    if (m_reflectY)
        y = m_sizeY - y - 1;
    switch (m_rotation % 4) {
    case 1:
        std::swap(x, y);
        y = m_sizeY - y - 1;
        break;
    case 2:
        x = m_sizeX - x - 1;
        y = m_sizeY - y - 1;
        break;
    case 3:
        std::swap(x, y);
        x = m_sizeX - x - 1;
        break;
    }
}

// The record's weight at a block column: the square of one less the normalized shape distance, 0
// outside the shape.
float Region::Record::influence(const Fixed &x, const Fixed &y)
{
    float weight = 1.0f - shapeDistance(x, y);
    if (weight <= 0.0f)
        return 0.0f;
    return weight * weight;
}

}
