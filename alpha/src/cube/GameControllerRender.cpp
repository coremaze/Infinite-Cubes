#include <windows.h>
#include <mmsystem.h>
#include "cube/WorldDimensions.h"
// Renders the world, creatures, effects and interface, with helpers for smoothing and cube effects.
#include <algorithm>
#include "cube/GameController.h"
#include "cube/WorldBlocks.h"
#include "cube/Chunk.h"
#include "cube/CubeShader.h"
#include "cube/Noise.h"
#include "cube/Sprite.h"
#include "cube/RaceIds.h"
#include "cube/SoundIds.h"
#include "cube/SpriteManager.h"
#include "cube/Vertex.h"
#include "plasma/Engine.h"
#include "plasma/Node.h"

#include "cube/SpriteIds.h"

namespace cube {
// The frame's tuning values (anonymous: this file's own); distances in blocks.
namespace {
enum {
    LampVisibilityRadius = 16
}; // a lamp's light (15 +- 0.5 blocks) is collected when this sphere shows
enum {
    FullMapZoneRadius = 16
}; // the world map draws the zones within it
enum {
    LightTimerPeriod = 100
}; // milliseconds
enum {
    AirshipVisibilityRadius = 64
};
enum {
    GroundSearchDepth = 100
}; // blocks a mark or a shadow looks down for the ground
}

template<> void blend(float &value, const float &target, int steps, float rate)
{
    float factor = blendFactor(steps, rate);
    value = value * (1.0f - factor) + target * factor;
}

// A light gathered for the frame.
struct FrameLight {
    plasma::Vector<3, Fixed> position;
    float radius;
    plasma::Vector<3, float> color;
    float distanceSquared; // from the camera
};

// A plant or zone object beyond its fade distance: render collects them (16 bytes; the constructor
// zeroes both pointers) and draws them translucent after the rest, farthest first.
struct DeferredSprite {
    DeferredSprite()
        : plant(0),
          object(0)
    {}
    const Plant *plant;
    Zone::ObjectState *object;
    float distanceSquared;
    int index; // the plant's in its chunk
};

// A creature with its squared distance from the camera (12 bytes), for drawing them farthest
// first.
struct CreatureDistance {
    Creature *creature;
    float distanceSquared;
    bool unused08; // false when collected; nothing reads it
};

// The night sky's vertices (FVF XYZ | DIFFUSE, 16 bytes) and the sun's (FVF XYZ | DIFFUSE | TEX1,
// 24 bytes).
struct StarVertex {
    plasma::Vector<3, float> position;
    D3DCOLOR color;
};
struct SunVertex {
    plasma::Vector<3, float> position;
    D3DCOLOR color;
    plasma::Vector<2, float> uv;
};

// The period of a calling plant (crickets, frogs) in which a position calls: its coordinates (y
// shifted by 333) shift the clock, so neighbours call out of step.
static int callPeriod(const plasma::Vector<3, Fixed> &position, int time, int period)
{
    return ((int)position[0] % period + (int)(position[1] + Fixed(333)) % period + time) / period;
}

// Renders one frame after update: swaps in completed chunk meshes, gathers visible chunks and lights, draws the sky or map or waiting screen, then clouds, chunks, creatures, objects, effects, water and interface. Light radii are in blocks; colors are RGB or RGBA in the range zero to one. Model turning rates use degrees per millisecond, and interface positions use pixels. The normal sky clear color is opaque blue; player-only screenshots use white.
void GameController::render()
{
    int startTime = timeGetTime();
    // The cloud sprite is tinted the sky's blue.
    Sprite *cloud = m_world.m_spriteManager.sprite(SpriteCloud02);
    if (cloud)
        cloud->m_tint = plasma::Vector<3, unsigned char>(0, 100, 200);
    m_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, m_drawPlayerOnly ? 0xffffffff : 0xff0070ff, 1.0f, 0);
    m_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    m_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    m_world.enterRegions();
    m_device->BeginScene();

    std::vector<Chunk *> visibleChunks;
    std::vector<FrameLight> lights;
    // 1 at noon, 0 at midnight.
    float daylight =
        pow(1.0f - pow((float)m_world.m_time * 2.0f / float(24 * World::MillisecondsPerHour) - 1.0f, 4.0f), 5.0f);

    // The chunks whose new meshes are complete: a remesh shares its time stamp with the other chunks
    // of the same pass, and a pass is shown only when none of its chunks is still waiting.
    std::map<int, int> pending;
    for (int i = 0; i < m_viewDistance * m_viewDistance; i++) {
        int remeshTime = m_chunks[i].remeshTime;
        if (remeshTime != 0 && m_chunks[i].needsRemesh)
            pending[remeshTime]++;
    }

    float range = m_loadedMeshRadius + 22.4f;
    for (int x = m_chunkOrigin[0]; x < m_chunkOrigin[0] + m_viewDistance; x++) {
        for (int y = m_chunkOrigin[1]; y < m_chunkOrigin[1] + m_viewDistance; y++) {
            if (x < 0 || y < 0 || x >= ChunksPerWorld || y >= ChunksPerWorld)
                continue;
            Chunk *chunk = &m_chunks[(y % m_viewDistance) * m_viewDistance + x % m_viewDistance];
            if (pending[chunk->remeshTime] == 0) {
                EnterCriticalSection(&chunk->lock);
                if (chunk->newChunkBuffers.size() != 0) {
                    for (std::list<ChunkBuffer *>::iterator it = chunk->chunkBuffers.begin();
                         it != chunk->chunkBuffers.end(); ++it)
                        delete *it;
                    chunk->chunkBuffers = chunk->newChunkBuffers;
                    chunk->newChunkBuffers.clear();
                    chunk->plants = chunk->newPlants;
                }
                LeaveCriticalSection(&chunk->lock);
            }

            EnterCriticalSection(&chunk->lock);
            if (chunk->chunkBuffers.size() != 0 && chunk->position == plasma::Vector<2, int>(x, y) &&
                isBoxVisible(chunk->boundsMin, chunk->boundsMax, range + BlocksPerChunk)) {
                visibleChunks.push_back(chunk);
                plasma::Vector<3, Fixed> offset = chunk->center - m_cameraPosition;
                chunk->squaredCameraDistance = (float)offset.dot(offset);
            }
            int phase = 0;
            for (std::list<Plant>::iterator plant = chunk->plants.begin(); plant != chunk->plants.end(); ++plant) {
                // The lamps light their surroundings, flickering (the radius 15 +- 0.5 blocks, 0.01 radians per
                // millisecond, each lamp 33 radians on).
                if ((plant->flags & (1 << PlantFlagEmissive)) &&
                    isSphereVisible(plant->position, LampVisibilityRadius, range)) {
                    phase += 33;
                    FrameLight light;
                    light.position =
                        plant->position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 0.0f));
                    light.radius = cos((float)m_world.m_clock * 0.01f + (float)phase) * 0.5f + 15.0f;
                    plasma::Vector<3, Fixed> offset = m_cameraPosition - light.position;
                    light.distanceSquared = (float)offset.dot(offset);
                    light.color = plant->color;
                    // Type 13 dims by day.
                    if (plant->type == PlantLantern02)
                        light.color *= 1.0f - daylight * 0.5f;
                    lights.push_back(light);
                }
                // The plants within 64 blocks call: birds in types 61..63 by day, owls in type 61 at night,
                // crickets in types 0..4 from noon to midnight: a bird or owl each second with a chance of one in
                // ten, at volume 0.1 and pitch 0.9..1.1 (owls 0.5..0.7), a cricket every five seconds with one in
                // 70, at volume 0.05.
                if (4096.0f > chunk->squaredCameraDistance) {
                    int clock = m_world.m_clock;
                    if (m_world.m_time > 6 * World::MillisecondsPerHour &&
                        m_world.m_time < 21 * World::MillisecondsPerHour) {
                        if (plant->type == PlantBirdAndOwlEmitter) {
                            if (callPeriod(plant->position, clock, 1000) !=
                                    callPeriod(plant->position, clock + m_frameMilliseconds, 1000) &&
                                rand() % 10 == 0)
                                playSound(SoundBird1, plant->position, 0.1f, rand() * 0.2f / float(RAND_MAX) + 0.9f);
                            continue;
                        }
                        if (plant->type == PlantBird2Emitter) {
                            if (callPeriod(plant->position, clock, 1000) !=
                                    callPeriod(plant->position, clock + m_frameMilliseconds, 1000) &&
                                rand() % 10 == 0)
                                playSound(SoundBird2, plant->position, 0.1f, rand() * 0.2f / float(RAND_MAX) + 0.9f);
                            continue;
                        }
                        if (plant->type == PlantBird3Emitter) {
                            if (callPeriod(plant->position, clock, 1000) !=
                                    callPeriod(plant->position, clock + m_frameMilliseconds, 1000) &&
                                rand() % 10 == 0)
                                playSound(SoundBird3, plant->position, 0.1f, rand() * 0.2f / float(RAND_MAX) + 0.9f);
                            continue;
                        }
                    } else if (plant->type == PlantBirdAndOwlEmitter) {
                        if (callPeriod(plant->position, clock, 1000) !=
                                callPeriod(plant->position, clock + m_frameMilliseconds, 1000) &&
                            rand() % 10 == 0) {
                            float speed = rand() * 0.2f / float(RAND_MAX) + 0.5f;
                            playSound(rand() % 2 + SoundOwl1, plant->position, 0.1f, speed);
                        }
                        continue;
                    }
                    if (m_world.m_time > 43200000 && m_world.m_time < 86400000) {
                        if (plant->type == PlantGrass || plant->type == PlantGrass2 || plant->type == PlantGrass3) {
                            if (callPeriod(plant->position, clock, 5000) !=
                                    callPeriod(plant->position, clock + m_frameMilliseconds, 5000) &&
                                rand() % 70 == 0)
                                playSound(SoundCricket1, plant->position, 0.05f,
                                          rand() * 0.2f / float(RAND_MAX) + 0.9f);
                        } else if (plant->type == PlantFlowers2 || plant->type == PlantFlowers) {
                            if (callPeriod(plant->position, clock, 5000) !=
                                    callPeriod(plant->position, clock + m_frameMilliseconds, 5000) &&
                                rand() % 70 == 0)
                                playSound(SoundCricket2, plant->position, 0.05f,
                                          rand() * 0.2f / float(RAND_MAX) + 0.9f);
                        }
                    }
                }
            }
            LeaveCriticalSection(&chunk->lock);
        }
    }

    // While "Please wait..." shows, only the interface is drawn, without the start menu's screens.
    if (m_waitNode->isVisible()) {
        bool startScreenVisible = m_startScreen->isVisible();
        bool characterCreationScreenVisible = m_characterCreationScreen->isVisible();
        bool characterListScreenVisible = m_characterListScreen->isVisible();
        bool worldListScreenVisible = m_worldListScreen->isVisible();
        bool worldCreationScreenVisible = m_worldCreationScreen->isVisible();
        m_startScreen->setVisible(false);
        m_characterCreationScreen->setVisible(false);
        m_characterListScreen->setVisible(false);
        m_worldListScreen->setVisible(false);
        m_worldCreationScreen->setVisible(false);
        m_gameScreen->setVisible(false);
        m_engine->drawWindow();
        m_startScreen->setVisible(startScreenVisible);
        m_characterCreationScreen->setVisible(characterCreationScreenVisible);
        m_characterListScreen->setVisible(characterListScreenVisible);
        m_worldListScreen->setVisible(worldListScreenVisible);
        m_worldCreationScreen->setVisible(worldCreationScreenVisible);
        m_device->EndScene();
        m_world.leaveRegions();
        return;
    }

    // The sky and the light: bluish under water.
    plasma::Vector<4, float> tint(1.0f, 1.0f, 1.0f, daylight);
    bool underwater =
        m_world.getBlock(m_cameraPosition[0], m_cameraPosition[1], m_cameraPosition[2], 0).type == BlockWater;
    if (underwater)
        tint = plasma::Vector<4, float>(0.4f, 0.5f, 1.0f, daylight);
    m_shader->setWhite(0.0f);
    m_shader->setMaterialColor(tint);
    m_shader->setAlpha(1.0f);
    plasma::Vector<4, float> skyColor0;
    plasma::Vector<4, float> skyColor1;
    plasma::Vector<4, float> skyColor2;
    plasma::Vector<4, float> frontLight;
    plasma::Vector<4, float> backLight;
    plasma::Vector<4, float> fogColor;
    m_world.lighting(m_cameraPosition, skyColor0, skyColor1, skyColor2, frontLight, backLight, fogColor);
    // The sun turns about the x axis once a day.
    float sunAngle = (float)((float)m_world.m_time * 2.0f * 3.141592653589793 / 86400000.0);
    plasma::Vector<3, float> lightDirection(0.0f, -sin(sunAngle), -cos(sunAngle));
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_shader->setCameraPosition(
        plasma::Vector<3, float>(m_cameraPosition + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0))));
    m_shader->setLight(frontLight, backLight, lightDirection, backLight);
    m_shader->setDarknessColor(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f));
    m_shader->setShininess(0.0f);
    m_shader->setSky(skyColor0, skyColor1, skyColor2);
    // The distances in blocks at which the plants fade out and up to which the creatures are drawn.
    float plantFadeEnd = m_viewRadius * 0.2f;
    float creatureRange = m_viewRadius * 0.3f;
    m_shader->setFogScale(m_loadedMeshRadius);
    m_shader->setFogGradientTranslation(m_mapOpen ? 90.0f : m_cameraRotation[0]);
    plasma::Vector<4, float> pointLightPositions[16];
    plasma::Vector<3, float> pointLightColors[16];
    for (int i = 0; i < 16; i++) {
        pointLightPositions[i] = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f);
        pointLightColors[i] = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    }
    m_shader->setPointLights(pointLightPositions, pointLightColors);

    // The background: a quad over the whole screen at the far plane.
    plasma::Matrix<float> screen;
    screen.identity();
    screen[0] = 2.0f;
    screen[5] = 2.0f;
    // Depth terms of a 0.1.. 10000 block range.
    screen[10] = 1.0f / (10000.0f - 0.1f);
    screen[11] = -0.1f / (10000.0f - 0.1f);
    m_device->SetRenderState(D3DRS_ZENABLE, FALSE);
    // The sky's quad lies at depth 100.
    Vertex background[4] = {
        Vertex(plasma::Vector<3, int>(0, 0, 100), plasma::Vector<3, int>(0, 0, 1),
               plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f)),
        Vertex(plasma::Vector<3, int>(0, 2, 100), plasma::Vector<3, int>(0, 0, 1),
               plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f)),
        Vertex(plasma::Vector<3, int>(2, 2, 100), plasma::Vector<3, int>(0, 0, 1),
               plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f)),
        Vertex(plasma::Vector<3, int>(2, 0, 100), plasma::Vector<3, int>(0, 0, 1),
               plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f)),
    };
    plasma::Matrix<float> identity;
    identity.identity();
    plasma::Matrix<float> model;
    model.identity();
    model.translate(-1.0f, -1.0f, 0.0f);
    m_shader->setMatrices(model, identity, screen);
    m_shader->applySky();
    if (!m_drawPlayerOnly)
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, background, sizeof(Vertex));

    // The world map (key M) replaces the world.
    if (m_mapOpen) {
        // It is tilted at least 110 degrees; its light comes from (0, 0.5, -1), with the back and ambient
        // colours (0.2, 0.3, 0.4) and 0.4.
        float tilt = std::min(-m_cameraRotation[0], -110.0f);
        plasma::Vector<3, float> rotation(tilt, 0.0f, -m_cameraRotation[2]);
        m_worldMap.render(m_player->m_entityData.position + plasma::Vector<3, Fixed>(m_mapOffset),
                          (int)(m_windowWidth * 0.5f), (int)(m_windowHeight * 0.5f), m_windowWidth, m_windowHeight,
                          rotation, m_mapZoom, m_frameMilliseconds, FullMapZoneRadius, true);
        m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
        plasma::Vector<3, float> direction(0.0f, 0.5f, -1.0f);
        direction *=
            1.0f / sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        m_shader->setLight(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f),
                           plasma::Vector<4, float>(0.2f, 0.3f, 0.4f, 1.0f), direction,
                           plasma::Vector<4, float>(0.4f, 0.4f, 0.4f, 1.0f));
        m_device->Clear(0, 0, D3DCLEAR_ZBUFFER, 0xff6464ff, 1.0f, 0);
        // The compass in the top right corner.
        drawSpriteModel((float)(m_windowWidth - 60), 40.0f,
                        plasma::Vector<3, float>(-120.0f, 0.0f, -m_cameraRotation[2]), 0.003f,
                        m_world.m_spriteManager.sprite(SpriteCompass2), 0.0f);
        m_gameScreen->setVisible(false);
        m_mapScreen->setVisible(true);
        m_engine->drawWindow();
        m_device->EndScene();
        m_world.leaveRegions();
        return;
    }

    // Shows this node while no start-menu screen is open.
    m_gameScreen->setVisible(!m_startScreen->isVisible() && !m_characterCreationScreen->isVisible() &&
                             !m_characterListScreen->isVisible() && !m_connectScreen->isVisible() &&
                             !m_worldListScreen->isVisible() && !m_worldCreationScreen->isVisible() &&
                             !m_waitNode->isVisible());
    m_mapScreen->setVisible(false);

    // The night sky with the fixed-function pipeline: the stars and the sun in view space, the
    // camera's rotation without its translation.
    m_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    m_device->SetVertexShader(0);
    m_device->SetPixelShader(0);
    plasma::Matrix<float> skyView = m_worldViewMatrix;
    skyView.translate(plasma::Vector<3, float>(m_cameraPosition));
    plasma::Matrix<float> skyProjection;
    // A narrower field of view while sneaking: 30 degrees instead of 50 (the planes 0.1 and 2000
    // blocks away).
    skyProjection.setPerspective(m_player->m_entityData.binaryToggles.sneaking ? 30.0f / 180.0f * 3.1415927f
                                                                               : 50.0f / 180.0f * 3.1415927f,
                                 -((float)m_windowWidth / (float)m_windowHeight), 0.1f, 2000.0f);
    m_device->SetTexture(0, 0);
    m_device->SetTransform(D3DTS_WORLD, (const D3DMATRIX *)&identity);
    m_device->SetTransform(D3DTS_VIEW, (const D3DMATRIX *)&identity);
    m_device->SetTransform(D3DTS_PROJECTION, (const D3DMATRIX *)&skyProjection);
    // The stars (x, y, z, size each) at night above water: a fan bright in the middle and transparent
    // at the corners.
    if (0.5f > daylight && !underwater) {
        for (int i = 0; i < (int)m_stars.size(); i++) {
            const plasma::Vector<4, float> &star = m_stars[i];
            plasma::Vector<3, float> center = skyView * plasma::Vector<3, float>(star[0], star[1], star[2]);
            float size = star[3];
            StarVertex fan[6];
            fan[0].position = center;
            fan[0].color = 0xffffffff;
            fan[1].position = center + plasma::Vector<3, float>(-1.0f, -1.0f, 0.0f) * size;
            fan[1].color = 0xffffff;
            fan[2].position = center + plasma::Vector<3, float>(1.0f, -1.0f, 0.0f) * size;
            fan[2].color = 0xffffff;
            fan[3].position = center + plasma::Vector<3, float>(1.0f, 1.0f, 0.0f) * size;
            fan[3].color = 0xffffff;
            fan[4].position = center + plasma::Vector<3, float>(-1.0f, 1.0f, 0.0f) * size;
            fan[4].color = 0xffffff;
            fan[5] = fan[1];
            m_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 4, fan, sizeof(StarVertex));
        }
    }
    // The sun: a textured quad 100 blocks along the light, yellower by day.
    m_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    m_device->SetTexture(0, m_sunTexture);
    m_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
    m_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
    // The sun: 100 blocks along the light, a quad 40 blocks to each side; its green 50 + 250 times the
    // daylight (at most 255), its blue 200 times the daylight squared.
    plasma::Vector<3, float> sun = skyView * (100.0f * lightDirection);
    D3DCOLOR sunColor =
        D3DCOLOR_XRGB(255, (int)std::min(daylight * 250.0f + 50.0f, 255.0f), (int)(daylight * 200.0f * daylight));
    SunVertex sunQuad[4] = {
        {sun + plasma::Vector<3, float>(-1.0f, -1.0f, 0.0f) * 40.0f, sunColor, plasma::Vector<2, float>(0.0f, 0.0f)},
        {sun + plasma::Vector<3, float>(1.0f, -1.0f, 0.0f) * 40.0f, sunColor, plasma::Vector<2, float>(1.0f, 0.0f)},
        {sun + plasma::Vector<3, float>(1.0f, 1.0f, 0.0f) * 40.0f, sunColor, plasma::Vector<2, float>(1.0f, 1.0f)},
        {sun + plasma::Vector<3, float>(-1.0f, 1.0f, 0.0f) * 40.0f, sunColor, plasma::Vector<2, float>(0.0f, 1.0f)},
    };
    m_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, sunQuad, sizeof(SunVertex));

    m_shader->apply();
    m_shader->setSky(skyColor0, skyColor1, skyColor2);
    m_projectionMatrix = skyProjection;
    plasma::Matrix<float> view = m_viewMatrix;
    m_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    // The view frustum's planes, for the visibility tests.
    plasma::Matrix<float> clip = m_projectionMatrix * m_viewMatrix;
    for (int i = 0; i < 4; i++)
        m_frustum[0][i] = clip(3, i) + clip(0, i);
    for (int i = 0; i < 4; i++)
        m_frustum[1][i] = clip(3, i) - clip(0, i);
    for (int i = 0; i < 4; i++)
        m_frustum[2][i] = clip(3, i) + clip(1, i);
    for (int i = 0; i < 4; i++)
        m_frustum[3][i] = clip(3, i) - clip(1, i);
    // Extract the far plane before the near plane.
    for (int i = 0; i < 4; i++)
        m_frustum[5][i] = clip(3, i) - clip(2, i);
    for (int i = 0; i < 4; i++)
        m_frustum[4][i] = clip(3, i) + clip(2, i);

    // The lights of the frame: the magic projectiles
    for (std::list<BulletPacket>::iterator it = m_world.m_projectiles.begin(); it != m_world.m_projectiles.end();
         ++it) {
        if (it->projectileType != ProjectileMagic)
            continue;
        FrameLight light;
        light.distanceSquared = (float)(m_cameraPosition - it->position).squaredLength();
        light.color = it->sizeInBlocks * plasma::Vector<3, float>(0.5f, 0.5f, 0.2f);
        light.position = it->position;
        light.radius = 16.0f;
        lights.push_back(light);
    }
    // The game's flashes, fading with their age
    for (std::list<Light>::iterator it = m_lights.begin(); it != m_lights.end(); ++it) {
        FrameLight light;
        light.distanceSquared = (float)(m_cameraPosition - it->position).squaredLength();
        light.color = it->color * std::max(0.0f, 1.0f - (float)it->age / (float)it->duration);
        light.position = it->position;
        light.radius = it->radius;
        lights.push_back(light);
    }
    // And the creatures' torches, lamps, buffs, glowing races and spells.
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         ++it) {
        Creature *creature = it->second;
        if (!creature || !(creature->m_entityData.hp > 0.0f))
            continue;
        Creature::EntityData &data = creature->m_entityData;
        // A torch in either hand.
        if (data.equipment.weaponRight.id == WeaponTorch || data.equipment.weaponLeft.id == WeaponTorch) {
            plasma::Matrix<float> turn;
            turn.identity();
            turn.rotateZ(data.rotation[2]);
            FrameLight light;
            light.position =
                creature->m_drawnPosition +
                plasma::Vector<3, Fixed>(turn * plasma::Vector<3, float>(0.5f, 1.0f, data.appearance.size[2] * 0.3f));
            light.radius = 32.0f;
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            light.color = plasma::Vector<3, float>(1.2f, 1.0f, 0.5f);
            lights.push_back(light);
        }
        // The lamp, lit (key F), brighter and wider by its rarity.
        if (data.binaryToggles.lampOn && data.equipment.lamp.category == ItemCategoryLamp) {
            plasma::Matrix<float> turn;
            turn.identity();
            turn.rotateZ(data.rotation[2]);
            FrameLight light;
            light.position =
                creature->m_drawnPosition +
                plasma::Vector<3, Fixed>(turn * plasma::Vector<3, float>(0.5f, 1.0f, data.appearance.size[2] * 0.3f));
            light.radius = (float)(data.equipment.lamp.rarity * 8 + 10);
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            light.color = plasma::Vector<3, float>(0.6f, 0.5f, 0.4f) * (data.equipment.lamp.rarity * 0.25f + 0.75f);
            lights.push_back(light);
        }
        // Buff 9 (a fire spell's).
        if (creature->findBuff(BuffInstantFire)) {
            plasma::Matrix<float> turn;
            turn.identity();
            turn.rotateZ(creature->m_entityData.rotation[2]);
            FrameLight light;
            light.position =
                creature->m_drawnPosition +
                plasma::Vector<3, Fixed>(turn * plasma::Vector<3, float>(0.5f, 1.0f, data.appearance.size[2] * 0.3f));
            light.radius = 16.0f;
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            light.color = plasma::Vector<3, float>(0.6f, 0.4f, 0.0f);
            lights.push_back(light);
        }
        // The lich and the deposits glow.
        if (data.race == RaceLich) {
            FrameLight light;
            light.position = creature->m_drawnPosition;
            light.radius = 16.0f;
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            light.color = plasma::Vector<3, float>(0.0f, 0.4f, 0.8f);
            lights.push_back(light);
        }
        if (data.race == RaceEmeraldDeposit) {
            FrameLight light;
            light.position = data.position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 1.5f));
            light.radius = 16.0f;
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            light.color = plasma::Vector<3, float>(0.1f, 0.5f, 0.1f);
            lights.push_back(light);
        }
        if (data.race == RaceSapphireDeposit) {
            FrameLight light;
            light.position = data.position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 1.5f));
            light.radius = 16.0f;
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            light.color = plasma::Vector<3, float>(0.1f, 0.1f, 0.5f);
            lights.push_back(light);
        }
        if (data.race == RaceRubyDeposit) {
            FrameLight light;
            light.position = data.position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 1.5f));
            light.radius = 16.0f;
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            light.color = plasma::Vector<3, float>(0.5f, 0.1f, 0.2f);
            lights.push_back(light);
        }
        if (data.race == RaceDiamondDeposit) {
            FrameLight light;
            light.position = data.position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 1.5f));
            light.radius = 16.0f;
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            light.color = plasma::Vector<3, float>(0.8f, 0.8f, 0.6f);
            lights.push_back(light);
        }
        // AppearanceFlagTinted: the creature's own colour.
        if (data.appearance.appearanceFlags & (1 << AppearanceFlagTinted)) {
            FrameLight light;
            light.position = creature->m_drawnPosition;
            light.radius = 16.0f;
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            plasma::Vector<4, float> color = creature->color();
            light.color = plasma::Vector<3, float>(color[0], color[1], color[2]) * 0.5f;
            lights.push_back(light);
        }
        // The spells light the hands: abilities 28, 94, 95 and 36 always, 87..89, 92, 37, 38, 43 and 44
        // while they are prepared.
        int ability = data.currentAbility;
        if (ability == AbilitySupportRay || ability == AbilityFireRay || ability == AbilityFireBeam ||
            ability == AbilityChargeFireball ||
            ((ability == AbilityExplosion || ability == AbilityFireExplosion || ability == AbilityLava ||
              ability == AbilitySummon || ability == AbilityFireball || ability == AbilityWandFirebolt ||
              ability == AbilityWaterSplash || ability == AbilityWandWaterDrop) &&
             data.timeSinceAbility < creature->anticipationTime())) {
            plasma::Matrix<float> turn;
            turn.identity();
            turn.rotateZ(creature->m_drawnRotation[2]);
            FrameLight light;
            light.position =
                creature->m_drawnPosition +
                plasma::Vector<3, Fixed>(turn.transformDirection(plasma::Vector<3, float>(0.0f, 1.0f, 0.0f)));
            light.radius = 20.0f;
            light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
            light.color =
                plasma::Vector<3, float>(0.3f, 0.2f, 0.1f) * (cos(data.timeSinceAbility * 0.01f) * 0.1f + 1.0f);
            lights.push_back(light);
        }
        // With a hit combo, the equipment that has an effect colour glows in it: the weapons at the start
        // of their trails, the shoulder and chest pieces at the creature.
        float combo = (float)data.hitCombo / (float)creature->maxCombo();
        if (combo == 0.0f)
            continue;
        Creature::AnimationState &animation = creature->m_animationState;
        if (data.equipment.weaponRight.category == ItemCategoryWeapon) {
            plasma::Vector<4, float> effect = data.equipment.weaponRight.effectColor();
            if (effect[3] > 0.0f) {
                FrameLight light;
                light.position = plasma::Vector<3, Fixed>(-animation.m_originX, -animation.m_originY, Fixed(0)) +
                                 plasma::Vector<3, Fixed>(animation.m_trails[0][0]);
                light.radius = 10.0f;
                light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
                plasma::Vector<3, float> glow(effect[0], effect[1], effect[2]);
                light.color = combo * glow * 0.3f;
                lights.push_back(light);
            }
        }
        if (data.equipment.weaponLeft.category == ItemCategoryWeapon) {
            plasma::Vector<4, float> effect = data.equipment.weaponLeft.effectColor();
            if (effect[3] > 0.0f) {
                FrameLight light;
                light.position = plasma::Vector<3, Fixed>(-animation.m_originX, -animation.m_originY, Fixed(0)) +
                                 plasma::Vector<3, Fixed>(animation.m_trails[2][0]);
                light.radius = 10.0f;
                light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
                plasma::Vector<3, float> glow(effect[0], effect[1], effect[2]);
                light.color = combo * glow * 0.5f;
                lights.push_back(light);
            }
        }
        if (data.equipment.shoulder.category != ItemCategoryNone) {
            plasma::Vector<4, float> effect = data.equipment.shoulder.effectColor();
            if (effect[3] > 0.0f) {
                FrameLight light;
                light.position = data.position;
                light.radius = 10.0f;
                light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
                plasma::Vector<3, float> glow(effect[0], effect[1], effect[2]);
                light.color = combo * glow * 0.1f;
                lights.push_back(light);
            }
        }
        if (data.equipment.chest.category != ItemCategoryNone) {
            plasma::Vector<4, float> effect = data.equipment.chest.effectColor();
            if (effect[3] > 0.0f) {
                FrameLight light;
                light.position = data.position;
                light.radius = 10.0f;
                light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
                plasma::Vector<3, float> glow(effect[0], effect[1], effect[2]);
                light.color = combo * glow * 0.1f;
                lights.push_back(light);
            }
        }
    }

    // The objects and items of the zones around the player that shine.
    static int s_lightTimer = 0;
    float effectRange = range * 0.2f;
    float entityFadeEnd = range * 0.3f;
    float entityFadeStart = entityFadeEnd * 0.9f;
    float plantFadeStart = plantFadeEnd * 0.9f;
    s_lightTimer += m_frameMilliseconds;
    if (s_lightTimer > LightTimerPeriod)
        s_lightTimer = 0;
    const plasma::Vector<3, Fixed> &center = m_player->m_entityData.position;
    int zoneX0 = (int)(((float)(int)center[0] - range) * (1.0f / float(BlocksPerZone)));
    int zoneX1 = (int)(((float)(int)center[0] + range) * (1.0f / float(BlocksPerZone)));
    int zoneY0 = (int)(((float)(int)center[1] - range) * (1.0f / float(BlocksPerZone)));
    int zoneY1 = (int)(((float)(int)center[1] + range) * (1.0f / float(BlocksPerZone)));
    for (int x = zoneX0; x <= zoneX1; x++) {
        for (int y = zoneY0; y <= zoneY1; y++) {
            Zone *zone = m_world.zone(x, y);
            if (!zone)
                continue;
            for (std::vector<Zone::Object>::iterator object = zone->m_objects.begin(); object != zone->m_objects.end();
                 object++) {
                // Type 50 lights at its top at night.
                if (object->type == StaticObjectStreetLight &&
                    isSphereVisible(object->position +
                                        plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, object->size[2])),
                                    20.0f, range)) {
                    FrameLight light;
                    light.position = object->position +
                                     plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, object->size[2]));
                    light.radius = 20.0f;
                    light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
                    light.color = plasma::Vector<3, float>(1.2f, 1.0f, 0.8f) * (1.0f - daylight);
                    lights.push_back(light);
                }
                // Types 51 and 65, warmer.
                if ((object->type == StaticObjectFireStreetLight || object->type == StaticObjectCampfire) &&
                    isSphereVisible(object->position, 32.0f, range)) {
                    FrameLight light;
                    light.position = object->position +
                                     plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, object->size[2]));
                    light.radius = 20.0f;
                    light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
                    light.color = plasma::Vector<3, float>(1.2f, 0.8f, 0.4f) * (1.0f - daylight);
                    lights.push_back(light);
                }
            }
            // The items on the ground that shine (and are not hidden).
            for (std::vector<Zone::ZoneItem>::iterator item = zone->m_items.begin(); item != zone->m_items.end();
                 item++) {
                if (item->hiddenTimeRemaining != 0 || !(item->item.lightColor().squaredLength() > 0.0f))
                    continue;
                plasma::Vector<3, Fixed> position(item->position[0], item->position[1], item->position[2] + 3);
                if (!isSphereVisible(position + plasma::Vector<3, Fixed>(Fixed(0), Fixed(0), Fixed(0)), 16.0f,
                                     plantFadeEnd))
                    continue;
                FrameLight light;
                light.position = position;
                light.radius = 16.0f;
                light.distanceSquared = (float)(m_cameraPosition - light.position).squaredLength();
                light.color = item->item.lightColor();
                lights.push_back(light);
            }
        }
    }

    // Nearest first; each light goes to the chunks it reaches, sixteen a chunk at most, and the
    // nearest sixteen light everything else.
    std::sort(lights.begin(), lights.end(),
              [this](const FrameLight &a, const FrameLight &b) { return a.distanceSquared < b.distanceSquared; });
    for (int i = 0; i < m_viewDistance * m_viewDistance; i++) {
        Chunk &chunk = m_chunks[i];
        chunk.lightCount = 0;
        for (int j = 0; j < 16; j++) {
            chunk.lightPositions[j] = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f);
            chunk.lightColors[j] = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        }
    }
    for (std::vector<FrameLight>::iterator light = lights.begin(); light != lights.end(); light++) {
        int x0 = (int)(light->position[0] - light->radius) / BlocksPerChunk;
        int y0 = (int)(light->position[1] - light->radius) / BlocksPerChunk;
        int x1 = (int)(light->position[0] + light->radius) / BlocksPerChunk;
        int y1 = (int)(light->position[1] + light->radius) / BlocksPerChunk;
        for (int x = x0; x <= x1; x++) {
            for (int y = y0; y <= y1; y++) {
                if (x < m_chunkOrigin[0] || y < m_chunkOrigin[1] || x >= m_chunkOrigin[0] + m_viewDistance ||
                    y >= m_chunkOrigin[1] + m_viewDistance || x < 0 || y < 0 || x >= ChunksPerWorld ||
                    y >= ChunksPerWorld)
                    continue;
                Chunk *chunk = &m_chunks[(y % m_viewDistance) * m_viewDistance + x % m_viewDistance];
                if (chunk->lightCount >= 16)
                    continue;
                plasma::Vector<3, float> position(light->position +
                                                  plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0)));
                chunk->lightPositions[chunk->lightCount] =
                    plasma::Vector<4, float>(position[0], position[1], position[2], light->radius);
                chunk->lightColors[chunk->lightCount] = light->color;
                chunk->lightCount++;
            }
        }
    }
    int lightCount = std::min((int)lights.size(), 16);
    for (int i = 0; i < lightCount; i++) {
        FrameLight &light = lights[i];
        plasma::Vector<3, float> position(light.position +
                                          plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0)));
        pointLightPositions[i] = plasma::Vector<4, float>(position[0], position[1], position[2], light.radius);
        pointLightColors[i] = light.color;
    }

    // The clouds, where the land is humid enough and not (meaning not established) above 0.01: one in
    // every 200-block cell of 13 x 13 around the camera that the noise allows.
    plasma::Vector<4, float> darkness = 0.5f * (skyColor0 + skyColor1);
    darkness[3] = daylight;
    m_shader->applyCloud();
    m_shader->setDarknessColor(darkness);
    m_shader->setFogScale(m_loadedMeshRadius * 1.5f);
    m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, daylight));
    m_shader->setLight(frontLight, frontLight, lightDirection, fogColor);
    if (!m_drawPlayerOnly &&
        m_world.blendedHumidity((int)m_player->m_entityData.position[0], (int)m_player->m_entityData.position[1]) >
            0.01f &&
        0.01f > m_world.blendedFirelands((int)m_player->m_entityData.position[0],
                                         (int)m_player->m_entityData.position[1])) {
        const plasma::Vector<3, Fixed> &camera = m_cameraPosition;
        for (int i = -6; i <= 6; i++) {
            for (int j = -6; j <= 6; j++) {
                int cellX = (int)camera[0] / 200 + i;
                int cellY = (int)camera[1] / 200 + j;
                float chance = (noise((double)cellX, (double)cellY) + 1.0f) * 0.5f;
                if (chance >= m_world.blendedHumidity(cellX, cellY) * 0.8f)
                    continue;
                plasma::Vector<3, Fixed> position(Fixed(cellX * 200), Fixed((cellX % 2 + cellY * 2) * 100), Fixed(0));
                if (0.3f > m_world.blendedHumidity((int)position[0], (int)position[1]))
                    continue;
                float lift = m_world.vegetationDensity((int)position[0], (int)position[1], 0);
                position[2] = Fixed(m_world.terrainHeight((int)position[0], (int)position[1], 0) + lift * 100.0f);
                if (position[2] < 0)
                    position[2] = Fixed(0);
                // The first coordinate uses the cell's x twice; the second uses its y.
                position[2] += Fixed(noise((double)(cellX * 16 + cellX), (double)(cellY * 19)) * 50.0f + 170.0f);
                plasma::Matrix<float> cloudModel;
                cloudModel.identity();
                cloudModel.translate(plasma::Vector<3, float>(
                    position + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0))));
                float scale = noise((double)(cellX * 16 - cellX), (double)(cellY * 23)) * 2.0f + 7.0f;
                cloudModel.scale(scale, scale, scale);
                cloudModel.rotateZ((float)((cellY + cellX) * 90));
                cloudModel.translate(cloud->sizeX() * -0.5f, cloud->sizeY() * -0.5f, 0.0f);
                m_shader->setMatrices(cloudModel, view, m_projectionMatrix);
                cloud->draw();
            }
        }
    }

    // The chunks, nearest first, each with the point lights that reach it.
    m_shader->apply();
    m_shader->setLight(frontLight, backLight, lightDirection, fogColor);
    m_shader->setFogScale(m_loadedMeshRadius);
    m_shader->setDarknessColor(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f));
    if (m_drawPlayerOnly)
        visibleChunks.clear();
    std::sort(visibleChunks.begin(), visibleChunks.end(),
              [this](Chunk *a, Chunk *b) { return a->squaredCameraDistance < b->squaredCameraDistance; });
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    // The plants and objects beyond their fade distance, drawn translucent after everything else,
    // farthest first.
    std::vector<DeferredSprite> deferred;
    for (std::vector<Chunk *>::iterator it = visibleChunks.begin(); it != visibleChunks.end(); it++) {
        Chunk *chunk = *it;
        EnterCriticalSection(&chunk->lock);
        if (!chunk->chunkBuffers.empty()) {
            m_shader->setMaterialColor(tint);
            m_shader->setPointLights(chunk->lightPositions, chunk->lightColors);
            m_shader->setAlpha(1.0f);
            for (std::list<ChunkBuffer *>::iterator buffer = chunk->chunkBuffers.begin();
                 buffer != chunk->chunkBuffers.end(); buffer++) {
                if (!(*buffer)->indexBuffer)
                    continue;
                // The chunk's origin is its coordinates times BlocksPerChunk (shifted by 5).
                plasma::Matrix<float> chunkModel;
                chunkModel.identity();
                chunkModel.translate((float)(Fixed(chunk->position[0] << 5) + m_drawOriginX),
                                     (float)(Fixed(chunk->position[1] << 5) + m_drawOriginY), (float)(*buffer)->baseZ);
                m_shader->setMatrices(chunkModel, view, m_projectionMatrix);
                m_device->SetStreamSource(0, (*buffer)->vertexBuffer, 0, sizeof(Vertex));
                m_device->SetIndices((*buffer)->indexBuffer);
                m_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, (*buffer)->vertexCount, 0,
                                               (*buffer)->primitiveCount);
            }
            // The chunk's plants, near enough.
            plasma::Vector<3, float> offset(chunk->center - m_cameraPosition);
            float limit = plantFadeEnd + BlocksPerChunk;
            if (!(offset.squaredLength() > limit * limit)) {
                int index = 0;
                for (std::list<Plant>::iterator plant = chunk->plants.begin(); plant != chunk->plants.end(); ++plant) {
                    index++;
                    // The canopy emitter sheds a coloured leaf now and then.
                    if (plant->type == PlantLeafEmitter) {
                        if ((m_world.getClock() + m_frameMilliseconds) / 1000 != m_world.getClock() / 1000 &&
                            rand() % 16 == 0) {
                            Particle mote;
                            mote.age = 0;
                            mote.position = plant->position;
                            // The original draws y, x in that order.
                            mote.velocity = 4.0f * plasma::Vector<3, float>(rand() / float(RAND_MAX) - 0.5f,
                                                                            rand() / float(RAND_MAX) - 0.5f, -0.5f);
                            mote.rotation = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                            // The original draws z, y, x in that order.
                            mote.spin = 10.0f * plasma::Vector<3, float>(rand() / float(RAND_MAX) - 0.5f,
                                                                         rand() / float(RAND_MAX) - 0.5f,
                                                                         rand() / float(RAND_MAX) - 0.5f);
                            mote.color =
                                plasma::Vector<4, float>(plant->color[0], plant->color[1], plant->color[2], 1.0f);
                            mote.size = 0.5f;
                            mote.type = ParticleLeaf;
                            mote.matrix.identity();
                            mote.lifetime = 20000;
                            mote.collidesWithBlocks = true;
                            m_particles.push_front(mote);
                        }
                        continue;
                    }
                    if (plant->type < 0 || plant->type >= (int)m_plantSprites.size() || plant->type == 18)
                        continue;
                    Sprite *sprite = m_plantSprites[plant->type];
                    if (!sprite)
                        continue;
                    float radius =
                        (float)(sprite->sizeX() > sprite->sizeZ() ? sprite->sizeX() : sprite->sizeZ()) * plant->scale;
                    if (!isSphereVisible(plant->position +
                                             plasma::Vector<3, Fixed>(Fixed(0), Fixed(0),
                                                                      Fixed(sprite->sizeZ() * 0.5f * plant->scale)),
                                         radius, plantFadeEnd))
                        continue;
                    float distanceSquared =
                        plasma::Vector<3, float>(
                            plant->position +
                            plasma::Vector<3, Fixed>(Fixed(0), Fixed(0), Fixed(sprite->sizeZ() * 0.5f * plant->scale)) -
                            m_cameraPosition)
                            .squaredLength();
                    if (distanceSquared > plantFadeStart * plantFadeStart) {
                        DeferredSprite later;
                        later.plant = &*plant;
                        later.distanceSquared = distanceSquared;
                        later.index = index;
                        deferred.push_back(later);
                        continue;
                    }
                    renderPlant(*plant, distanceSquared, plantFadeEnd, plantFadeStart, index, view, m_projectionMatrix,
                                tint);
                }
            }
        }
        LeaveCriticalSection(&chunk->lock);
    }
    m_shader->setPointLights(pointLightPositions, pointLightColors);

    // The items on the ground and the objects of the zones around the player.
    if (!m_drawPlayerOnly) {
        for (int x = zoneX0; x <= zoneX1; x++) {
            for (int y = zoneY0; y <= zoneY1; y++) {
                Zone *zone = m_world.zone(x, y);
                if (!zone)
                    continue;
                int index = -1;
                for (std::vector<Zone::ZoneItem>::iterator item = zone->m_items.begin(); item != zone->m_items.end();
                     item++) {
                    index++;
                    if (item->hiddenTimeRemaining != 0)
                        continue;
                    Sprite *sprite = m_world.m_spriteManager.itemSprite(&item->item);
                    float radius = (float)sprite->sizeX() * 0.7f * item->scale;
                    plasma::Vector<3, Fixed> position = item->position;
                    // A dropped item bounces once.
                    if (item->dropTime > 0) {
                        float bounce = (float)(item->dropTime - 250) / 250.0f;
                        position[2] += Fixed((1.0f - bounce * bounce) * 5.0f);
                    }
                    if (!m_player->canUseFormula(item->item))
                        continue;
                    if (!isSphereVisible(position + plasma::Vector<3, Fixed>(
                                                        Fixed(0), Fixed(0), Fixed(sprite->sizeZ() * 0.5 * item->scale)),
                                         radius, entityFadeEnd))
                        continue;
                    plasma::Matrix<float> itemModel;
                    itemModel.identity();
                    itemModel.translate((float)(position[0] + m_drawOriginX), (float)(position[1] + m_drawOriginY),
                                        (float)position[2]);
                    itemModel.scale(item->scale, item->scale, item->scale);
                    // In the light of its block.
                    plasma::Vector<4, float> color(1.0f, 1.0f, 1.0f,
                                                   m_world.cubeLight((int)item->position[0], (int)item->position[1],
                                                                     (int)item->position[2], 0, 0) /
                                                       255.0f * daylight);
                    if ((item->item.category == ItemCategoryMaterial && item->item.id == MaterialItemShimmerMushroom) ||
                        item->item.category == ItemCategoryCandle)
                        color[3] = 1.0f;
                    switch (item->item.category) {
                    case ItemCategoryManaCube:
                        // Turning and tilted on a corner.
                        itemModel.rotateZ(m_world.getClock() * 0.02f);
                        itemModel.translate(0.0f, 0.0f, (float)sprite->sizeZ());
                        itemModel.rotateX(45.0f);
                        itemModel.rotateY(45.0f);
                        itemModel.translate(sprite->sizeX() * -0.5f, sprite->sizeY() * -0.5f, sprite->sizeZ() * -0.5f);
                        break;
                    case ItemCategoryWeapon:
                    case ItemCategoryChest:
                        // Standing on end.
                        itemModel.rotateZ(item->rotation);
                        itemModel.translate(0.0f, 0.0f, sprite->sizeX() * 0.5f);
                        itemModel.rotateY(90.0f);
                        itemModel.translate(sprite->sizeX() * -0.5f, sprite->sizeY() * -0.5f, sprite->sizeZ() * -0.5f);
                        break;
                    case ItemCategoryCoin:
                    case ItemCategoryPlatinumCoin: {
                        // Pulsing and turning, standing on end.
                        float pulse =
                            cos((float)(m_world.getClock() * 2 * 0.2f * 3.141592653589793 / 180.0)) * 0.5f + 0.5f;
                        color *= pulse * pulse + 1.0f;
                        itemModel.rotateZ(m_world.getClock() * 0.2f + item->rotation);
                        itemModel.translate(0.0f, 0.0f, sprite->sizeX() * 0.5f);
                        itemModel.rotateY(90.0f);
                        itemModel.translate(sprite->sizeX() * -0.5f, sprite->sizeY() * -0.5f, sprite->sizeZ() * -0.5f);
                        break;
                    }
                    default:
                        // Lying flat.
                        itemModel.rotateZ(item->rotation);
                        itemModel.translate(sprite->sizeX() * -0.5f, sprite->sizeY() * -0.5f, 0.0f);
                        break;
                    }
                    // Tumbling while it bounces.
                    if (item->dropTime != 0) {
                        itemModel.translate(sprite->sizeX() * 0.5f, sprite->sizeY() * 0.5f, sprite->sizeZ() * 0.5f);
                        itemModel.rotateX((float)item->dropTime);
                        itemModel.rotateY(item->dropTime * 0.7f);
                        itemModel.translate(sprite->sizeX() * -0.5f, sprite->sizeY() * -0.5f, sprite->sizeZ() * -0.5f);
                    }
                    // The item under the pointer is lighter.
                    plasma::Vector<4, float> itemColor = color;
                    if (plasma::Vector<3, int>(x, y, index) == m_hoveredGroundItem)
                        itemColor += plasma::Vector<4, float>(0.25f, 0.25f, 0.25f, 0.25f);
                    m_shader->setMatrices(itemModel, view, m_projectionMatrix);
                    m_shader->setMaterialColor(itemColor);
                    sprite->draw();
                    drawCubes(&item->item, itemModel, view, m_projectionMatrix, itemColor, 0.0f);
                }

                index = -1;
                for (std::vector<Zone::Object>::iterator object = zone->m_objects.begin();
                     object != zone->m_objects.end(); object++) {
                    index++;
                    if (object->type < 0 || object->type >= (int)m_staticEntitySprites.size())
                        continue;
                    float extent = object->size[0];
                    if (object->size[1] > extent)
                        extent = object->size[1];
                    if (object->size[2] > extent)
                        extent = object->size[2];
                    if (!isSphereVisible(object->position +
                                             plasma::Vector<3, Fixed>(Fixed(0), Fixed(0), Fixed(object->size[2] * 0.5)),
                                         extent + 1.0f, entityFadeEnd))
                        continue;
                    // A burning fire trap spits flames to its front.
                    if (object->type == StaticObjectFireTrap && object->active) {
                        plasma::Vector<3, float> flame(8.0f, 0.0f, 0.0f);
                        switch (object->rotation) {
                        case 1:
                            flame = plasma::Vector<3, float>(0.0f, 8.0f, 0.0f);
                            break;
                        case 2:
                            flame = plasma::Vector<3, float>(-8.0f, 0.0f, 0.0f);
                            break;
                        case 3:
                            flame = plasma::Vector<3, float>(0.0f, -8.0f, 0.0f);
                            break;
                        }
                        m_shader->setAlpha(1.0f);
                        drawImpact(object->position - plasma::Vector<3, Fixed>(0.5f * flame), flame,
                                   plasma::Vector<3, float>(0.0f, 0.0f, 0.0f),
                                   plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f),
                                   plasma::Vector<4, float>(1.0f, 0.25f, 0.0f, 1.0f),
                                   plasma::Vector<4, float>(1.0f, 0.5f, 0.0f, 1.0f), m_world.getClock(), view,
                                   m_projectionMatrix, 0.2f, 4.0f, 2.0f, 40, false);
                    }
                    float distanceSquared =
                        plasma::Vector<3, float>(object->position - m_cameraPosition).squaredLength();
                    if (distanceSquared > entityFadeStart * entityFadeStart) {
                        DeferredSprite later;
                        later.object = &*object;
                        later.distanceSquared = distanceSquared;
                        deferred.push_back(later);
                        continue;
                    }
                    // The object under the pointer is lighter.
                    plasma::Vector<4, float> objectColor = tint;
                    if (plasma::Vector<3, int>(x, y, index) == m_hoveredObject)
                        objectColor += plasma::Vector<4, float>(0.1f, 0.1f, 0.1f, 0.1f);
                    renderStaticEntity(*object, distanceSquared, entityFadeEnd, entityFadeStart, view,
                                       m_projectionMatrix, objectColor);
                    // Type 65 is ringed by rising fire.
                    if (object->type == StaticObjectCampfire)
                        drawRisingCubeRing(object->position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                                  0.0f, 0.0f, object->size[2] * 0.5f)),
                                           plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f),
                                           plasma::Vector<4, float>(1.0f, 1.0f, 0.0f, 1.0f), m_world.getClock(), view,
                                           m_projectionMatrix, 0.8f, 0.2f, 20);
                }
            }
        }
    }
    m_shader->setAlpha(1.0f);
    std::sort(deferred.begin(), deferred.end(), [this](const DeferredSprite &a, const DeferredSprite &b) {
        return a.distanceSquared > b.distanceSquared;
    });

    // The building mode : the selected building piece at the block the player aims at, pulsing.
    if (m_buildingMode) {
        Sprite *piece = m_world.buildingSprite(m_player->m_entityData.buildingModel);
        if (piece) {
            plasma::Vector<3, int> block(m_player->m_entityData.position +
                                         plasma::Vector<3, Fixed>(m_player->m_entityData.attackRotation) +
                                         plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.5f, 0.5f, 0.5f)));
            plasma::Matrix<float> pieceModel;
            pieceModel.identity();
            pieceModel.translate(plasma::Vector<3, float>(
                plasma::Vector<3, Fixed>(block) +
                plasma::Vector<3, Fixed>(m_drawOriginX + 0.01f, m_drawOriginY + 0.01f, Fixed(0.01f))));
            pieceModel.rotateZ((signed char)m_player->m_entityData.interactionState * 90.0f);
            pieceModel.translate((float)-(piece->sizeX() / 2), (float)-(piece->sizeY() / 2), 0.0f);
            m_shader->setMatrices(pieceModel, view, m_projectionMatrix);
            m_shader->setMaterialColor(
                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, (cos(m_world.getClock() * 0.005f) + 1.0f) * 0.25f + 0.5f));
            piece->draw();
        }
    }

    // The airships.
    for (std::map<long long, Zone::Airship *>::iterator it = m_world.m_airships.begin(); it != m_world.m_airships.end();
         ++it) {
        Zone::Airship *airship = it->second;
        if (!airship || !isSphereVisible(airship->position, AirshipVisibilityRadius, range))
            continue;
        Sprite *sprite = m_world.m_spriteManager.sprite(SpriteAirship);
        if (!sprite)
            continue;
        plasma::Matrix<float> airshipModel;
        airshipModel.identity();
        airshipModel.translate((float)(airship->currentPosition[0] + m_drawOriginX),
                               (float)(airship->currentPosition[1] + m_drawOriginY),
                               (float)airship->currentPosition[2]);
        airshipModel.rotateZ(airship->currentRotation);
        airshipModel.translate(sprite->sizeX() * -0.5f, sprite->sizeY() * -0.5f, sprite->sizeZ() * -0.5f);
        m_shader->setMaterialColor(tint);
        m_shader->setMatrices(airshipModel, view, m_projectionMatrix);
        sprite->draw();
    }

    // The creatures, farthest first (none behind the start menu's screens).
    std::vector<CreatureDistance> creatures;
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         ++it) {
        if (!it->second)
            continue;
        CreatureDistance entry;
        entry.creature = it->second;
        entry.distanceSquared =
            plasma::Vector<3, float>(it->second->m_drawnPosition - m_cameraPosition).squaredLength();
        entry.unused08 = false;
        creatures.push_back(entry);
    }
    if (m_startScreen->isVisible() || m_characterListScreen->isVisible() || m_connectScreen->isVisible() ||
        m_worldListScreen->isVisible())
        creatures.clear();
    std::sort(creatures.begin(), creatures.end(), [](const CreatureDistance &a, const CreatureDistance &b) {
        return a.distanceSquared > b.distanceSquared;
    });
    std::list<Creature *> stealthyCreatures;
    for (std::vector<CreatureDistance>::iterator it = creatures.begin(); it != creatures.end(); it++) {
        Creature *creature = it->creature;
        Creature::EntityData &data = creature->m_entityData;
        Creature::AnimationState &animation = creature->m_animationState;
        // A creature that died bursts into particles once (when it was drawn and is in view); a bomb
        // flashes.
        if (!(data.hp > 0.0f)) {
            float height = data.appearance.size[2];
            if (data.currentAbility == AbilityRidePet)
                height *= 3.0f;
            if (animation.m_hasSpriteUpdate &&
                isSphereVisible(creature->m_drawnPosition + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                                0.0f, 0.0f, -creature->m_stepOffset)),
                                height, creatureRange)) {
                createParticles(animation.m_headSprite, animation.m_headMatrix, plasma::Vector<3, float>(data.position),
                                data.velocity, 1.0f, (float)animation.m_originX, (float)animation.m_originY,
                                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), ParticleLitCube);
                // The hair bursts at the head's matrix, not its own.
                if (!(data.appearance.appearanceFlags & (1 << AppearanceFlagSuppressHairDebris)))
                    createParticles(animation.m_hairSprite, animation.m_headMatrix,
                                    plasma::Vector<3, float>(data.position), data.velocity, 1.0f,
                                    (float)animation.m_originX, (float)animation.m_originY,
                                    plasma::Vector<4, float>(data.appearance.hairColor[0] / 255.0f,
                                                             data.appearance.hairColor[1] / 255.0f,
                                                             data.appearance.hairColor[2] / 255.0f, 1.0f),
                                    ParticleLitCube);
                createParticles(animation.m_chestSprite, animation.m_chestMatrix,
                                plasma::Vector<3, float>(data.position), data.velocity, 1.0f,
                                (float)animation.m_originX, (float)animation.m_originY,
                                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), ParticleLitCube);
                createParticles(animation.m_handsSprite, animation.m_leftHandMatrix,
                                plasma::Vector<3, float>(data.position), data.velocity, 1.0f,
                                (float)animation.m_originX, (float)animation.m_originY,
                                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), ParticleLitCube);
                createParticles(animation.m_handsSprite, animation.m_rightHandMatrix,
                                plasma::Vector<3, float>(data.position), data.velocity, 1.0f,
                                (float)animation.m_originX, (float)animation.m_originY,
                                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), ParticleLitCube);
                createParticles(animation.m_shouldersSprite, animation.m_leftShoulderMatrix,
                                plasma::Vector<3, float>(data.position), data.velocity, 1.0f,
                                (float)animation.m_originX, (float)animation.m_originY,
                                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), ParticleLitCube);
                createParticles(animation.m_shouldersSprite, animation.m_rightShoulderMatrix,
                                plasma::Vector<3, float>(data.position), data.velocity, 1.0f,
                                (float)animation.m_originX, (float)animation.m_originY,
                                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), ParticleLitCube);
                createParticles(animation.m_feetSprite, animation.m_leftFootMatrix,
                                plasma::Vector<3, float>(data.position), data.velocity, 1.0f,
                                (float)animation.m_originX, (float)animation.m_originY,
                                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), ParticleLitCube);
                createParticles(animation.m_feetSprite, animation.m_rightFootMatrix,
                                plasma::Vector<3, float>(data.position), data.velocity, 1.0f,
                                (float)animation.m_originX, (float)animation.m_originY,
                                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), ParticleLitCube);
                createParticles(animation.m_rightWeaponSprite, animation.m_rightWeaponMatrix,
                                plasma::Vector<3, float>(data.position), data.velocity, 1.0f,
                                (float)animation.m_originX, (float)animation.m_originY,
                                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), ParticleLitCube);
                if (data.race == RaceBomb) {
                    Light flash;
                    flash.position = data.position;
                    flash.radius = 16.0f;
                    flash.color = plasma::Vector<3, float>(1.0f, 0.8f, 0.5f);
                    flash.age = 0;
                    flash.duration = 800;
                    m_lights.push_back(flash);
                }
            }
            animation.m_hasSpriteUpdate = false;
            continue;
        }
        animation.m_hasSpriteUpdate = false;

        // The abilities 30..33: a spiral of cubes around the point aimed at, which widens while the
        // ability lasts; a second, wider one for 31 and 33. Red to yellow, or blue for specialization 1.
        if ((data.currentAbility == AbilityFireSwirl || data.currentAbility == AbilityFireVortex ||
             data.currentAbility == AbilityWaterSwirl || data.currentAbility == AbilityWaterVortex) &&
            data.timeSinceAbility < creature->attackDuration() &&
            isSphereVisible(data.position + plasma::Vector<3, Fixed>(creature->m_drawnAim), 4.0f, creatureRange)) {
            float turns = (float)data.timeSinceAbility / (float)creature->anticipationTime() * 0.1f;
            float radius = (float)data.timeSinceAbility * 2.0f / (float)creature->attackDuration();
            int count = 43;
            if (data.timeSinceAbility > creature->anticipationTime()) {
                turns *= 2.0f;
                radius *= 2.0f;
                count = 86;
            }
            plasma::Vector<4, float> startColor(1.0f, 0.0f, 0.0f, 1.0f);
            plasma::Vector<4, float> endColor(1.0f, 1.0f, 0.0f, 1.0f);
            if (data.specialization == SpecializationWater) {
                startColor = plasma::Vector<4, float>(0.0f, 0.1f, 1.0f, 1.0f);
                endColor = plasma::Vector<4, float>(0.5f, 0.7f, 1.0f, 1.0f);
            }
            drawExpandingCubeSpiral(
                data.position - plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, radius * 0.5f)) +
                    plasma::Vector<3, Fixed>(creature->m_drawnAim),
                startColor, endColor, data.timeSinceAbility, view, m_projectionMatrix, radius,
                (float)data.timeSinceAbility * 0.4f / (float)creature->attackDuration(), turns, count);
            if (data.currentAbility == AbilityFireVortex || data.currentAbility == AbilityWaterVortex)
                drawExpandingCubeSpiral(
                    data.position - plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, radius)) +
                        plasma::Vector<3, Fixed>(creature->m_drawnAim),
                    startColor, endColor, data.timeSinceAbility, view, m_projectionMatrix, radius * 2.0f,
                    (float)data.timeSinceAbility * 0.4f / (float)creature->attackDuration(), turns * 0.5f, count * 2);
        }

        // Ability 34 (the heal beam) rings its target with rising cubes.
        if (data.focusId != 0 && data.currentAbility == AbilityHealingStream &&
            data.timeSinceAbility < creature->attackDuration()) {
            Creature *target = m_world.creatureByID(data.focusId);
            if (target &&
                isSphereVisible(target->m_drawnPosition, target->m_entityData.appearance.size[2], creatureRange))
                drawRisingCubeRing(target->m_drawnPosition, plasma::Vector<4, float>(0.0f, 0.0f, 1.0f, 1.0f),
                                   plasma::Vector<4, float>(0.25f, 0.75f, 1.0f, 1.0f), data.timeSinceAbility, view,
                                   m_projectionMatrix, target->m_entityData.appearance.size[0], 0.1f, 20);
        }

        // The rays (ability 28 with mana, 94 and 95 while they last), once the anticipation is over and
        // unless the creature is stunned: a stretched cube from the head (AppearanceFlagHeadAimOrigin) or
        // the right hand to where the ray along the aim ends.
        if (((data.currentAbility == AbilitySupportRay && data.mp > 0.0f) ||
             ((data.currentAbility == AbilityFireRay || data.currentAbility == AbilityFireBeam) &&
              data.timeSinceAbility < creature->attackDuration())) &&
            data.timeSinceAbility >= creature->anticipationTime() && data.stunTime <= 0 &&
            isSphereVisible(data.position, 200.0f, creatureRange)) {
            plasma::Vector<3, float> direction = creature->m_drawnAim.normalized();
            plasma::Vector<3, Fixed> eye = creature->sightPosition();
            float reach = m_world.rayLength(eye, direction, 100.0f, false, true);
            plasma::Vector<3, Fixed> target = eye + plasma::Vector<3, Fixed>(reach * direction);
            plasma::Vector<3, Fixed> origin;
            if ((data.appearance.appearanceFlags & (1 << AppearanceFlagHeadAimOrigin)) && animation.m_headSprite) {
                Sprite *head = animation.m_headSprite;
                origin = plasma::Vector<3, Fixed>(-animation.m_originX, -animation.m_originY, Fixed(0)) +
                         plasma::Vector<3, Fixed>(
                             animation.m_headMatrix *
                             (0.5f * plasma::Vector<3, float>((float)head->sizeX(), (float)head->sizeY(),
                                                              (float)head->sizeZ())));
            } else if (animation.m_handsSprite) {
                Sprite *hands = animation.m_handsSprite;
                origin = plasma::Vector<3, Fixed>(-animation.m_originX, -animation.m_originY, Fixed(0)) +
                         plasma::Vector<3, Fixed>(
                             animation.m_rightHandMatrix *
                             (0.5f * plasma::Vector<3, float>((float)hands->sizeX(), (float)hands->sizeY(),
                                                              (float)hands->sizeZ())));
            } else {
                origin =
                    plasma::Vector<3, Fixed>(-animation.m_originX, -animation.m_originY, Fixed(0)) +
                    plasma::Vector<3, Fixed>(animation.m_rightHandMatrix * plasma::Vector<3, float>(0.0f, 0.0f, 0.0f));
            }
            plasma::Matrix<float> rayModel;
            rayModel.identity();
            rayModel.translate((float)(origin[0] + m_drawOriginX), (float)(origin[1] + m_drawOriginY),
                               (float)origin[2]);
            // The ray's +Y axis turned onto the line to the target.
            rayModel.rotateTowards(plasma::Vector<3, float>(0.0f, 1.0f, 0.0f),
                                   plasma::Vector<3, float>(target - origin));
            float width = 0.1f;
            if (data.currentAbility == AbilityFireRay)
                width = 0.25f;
            // The ray grows out over a tenth of a second.
            float growth;
            if (1.0f > (data.timeSinceAbility - creature->anticipationTime()) * 0.01f)
                growth = (data.timeSinceAbility - creature->anticipationTime()) * 0.01f;
            else
                growth = 1.0f;
            float length = plasma::Vector<3, float>(target - origin).length();
            rayModel.scale(width, length * growth, width);
            rayModel.translate(-0.5f, 0.0f, -0.5f);
            m_shader->setMatrices(rayModel, view, m_projectionMatrix);
            // Pulsing in the colour of the specialization.
            float red;
            float green;
            float blue;
            switch (data.specialization) {
            case SpecializationWater:
                blue = 1.0f;
                green = cos(m_world.getClock() * 0.01f) * 0.3f + 0.3f;
                red = cos(m_world.getClock() * 0.01f) * 0.3f + 0.4f;
                break;
            case Specialization2:
                blue = cos(m_world.getClock() * 0.01f) * 0.3f + 0.4f;
                green = 1.0f;
                red = cos(m_world.getClock() * 0.01f) * 0.3f + 0.3f;
                break;
            default:
                blue = 0.4f;
                green = (float)(cos(m_world.getClock() * 0.03f) * 0.3f + 0.7);
                red = 1.0f;
                break;
            }
            m_shader->setMaterialColor(plasma::Vector<4, float>(red, green, blue, 1.0f));
            m_cubeSprite->draw();
        } else if (!isSphereVisible(data.position, data.appearance.size[2], creatureRange)) {
            // Nothing more for a creature out of view.
            continue;
        }
        // With m_drawPlayerOnly set only the player is drawn.
        if (m_drawPlayerOnly && creature != m_player)
            continue;

        // The animation of the ability in progress, in the light of the block below the creature.
        float light =
            m_world.cubeLight((int)data.position[0], (int)data.position[1], (int)(data.position[2] - 0.1f), 0, 0) /
            255.0f;
        animation.m_animationTime = data.timeSinceAbility;
        switch (data.currentAbility) {
        case AbilityRightSlash:
            animation.m_animation = AnimationRightSlash;
            break;
        case AbilityLeftSlash:
            animation.m_animation = AnimationLeftSlash;
            break;
        case Ability3:
        case AbilityShieldAttack:
        case AbilityRightDaggerPierce:
        case Ability62:
            animation.m_animation = AnimationLeftBackhand;
            break;
        case Ability4:
        case AbilityLeftDaggerPierce:
            animation.m_animation = AnimationRightBackhand;
            break;
        case AbilityPerforate:
            animation.m_animation = AnimationPerforate;
            break;
        case AbilityRightStab:
            animation.m_animation = AnimationRightStab;
            break;
        case AbilityLeftStab:
            animation.m_animation = AnimationLeftStab;
            break;
        case AbilityBlock:
            animation.m_animation = AnimationBlock;
            break;
        case AbilityBlockJab:
            animation.m_animation = AnimationBlockJab;
            break;
        case AbilitySwirlStrike:
            animation.m_animation = AnimationSwirlStrike;
            break;
        case AbilityCrossedWeaponSweep:
            animation.m_animation = AnimationCrossedWeaponSweep;
            break;
        case AbilityLeftSweep:
            animation.m_animation = AnimationLeftSweep;
            break;
        case AbilityRightSweep:
            animation.m_animation = AnimationRightSweep;
            break;
        case AbilityOverheadChop:
            animation.m_animation = AnimationOverheadChop;
            break;
        case AbilityMonsterChargeStrike:
        case AbilityAmbush:
            animation.m_animation = AnimationAmbush;
            break;
        case AbilityKick:
        case AbilityRangerKick:
            animation.m_animation = AnimationKick;
            break;
        case AbilityShoot:
        case AbilityChargedShotRelease:
            animation.m_animation = AnimationDrawBow;
            break;
        case AbilityChargedShot:
        case AbilitySalvo:
        case AbilityRetreat:
        case AbilitySalvoRelease:
            animation.m_animation = AnimationShoot;
            break;
        case AbilityThrowBoomerang:
            animation.m_animation = AnimationThrowBoomerang;
            break;
        case AbilityChargeBoomerang:
            animation.m_animation = AnimationChargedBoomerang;
            break;
        case AbilitySupportRay:
            animation.m_animation = AnimationSupportRay;
            break;
        case AbilityFireSwirl:
        case AbilityFireVortex:
        case AbilityWaterSwirl:
        case AbilityWaterVortex:
        case AbilityHealingStream:
        case AbilityTeleport:
        case AbilityExplosion:
        case AbilityFireExplosion:
        case AbilityLava:
        case AbilityWaterCloud:
        case AbilitySummon:
        case AbilityTeleportToCity:
            animation.m_animation = AnimationCastSpell;
            break;
        case Ability35:
            animation.m_animation = AnimationHealingStream;
            break;
        case AbilityChargeFireball:
            animation.m_animation = AnimationFireballCharge;
            break;
        case AbilityFireball:
        case AbilityWaterSplash:
            animation.m_animation = AnimationFireball;
            break;
        case AbilityWandFirebolt:
        case AbilityWandWaterDrop:
        case AbilityWaterSalvo:
        case AbilityFireSalvo:
        case AbilityFireBeam:
        case AbilityFireRay:
            animation.m_animation = AnimationFirebolt;
            break;
        case AbilityFirebolt:
        case AbilityWaterDrop:
            animation.m_animation = AnimationLeftCast;
            break;
        case AbilityBraceletFirebolt:
        case AbilityBraceletWaterDrop:
            animation.m_animation = AnimationRightCast;
            break;
        case Ability47:
        case AbilitySmash:
            animation.m_animation = AnimationSmash;
            break;
        case AbilityIntercept:
            // The bow with arrows (right weapon ids 6 and 7), a weapon in the left hand (id 13 apart), or
            // none.
            if (data.equipment.weaponRight.category == ItemCategoryWeapon &&
                (data.equipment.weaponRight.id == WeaponBow || data.equipment.weaponRight.id == WeaponCrossbow))
                animation.m_animation = AnimationShoot;
            else if (data.equipment.weaponLeft.category == ItemCategoryWeapon &&
                     data.equipment.weaponLeft.id == WeaponShield)
                animation.m_animation = AnimationBlock;
            else if (data.equipment.weaponLeft.category == ItemCategoryWeapon)
                animation.m_animation = AnimationAmbush;
            else
                animation.m_animation = AnimationIntercept;
            break;
        case AbilityForwardRoll:
            animation.m_animation = AnimationDiveBite;
            break;
        case AbilityTwoHandedSweep:
        case AbilityCrushStrike:
            animation.m_animation = AnimationTwoHandedSweep;
            break;
        case AbilityTwoHandedThrust:
            animation.m_animation = AnimationTwoHandedThrust;
            break;
        case AbilityCrush:
            animation.m_animation = AnimationCrush;
            break;
        case AbilityGuardianCrushStrike:
        case AbilityTwoHandedSlam:
            animation.m_animation = AnimationGuardianCrushStrike;
            break;
        case AbilitySwirl:
            animation.m_animation = AnimationSwirl;
            break;
        case AbilityMonsterCharge:
            animation.m_animation = AnimationMonsterCharge;
            break;
        case AbilityTwoHandedSpin:
            animation.m_animation = AnimationTwoHandedSpin;
            break;
        case AbilityTwoHandedSwing:
            animation.m_animation = AnimationTwoHandedSwing;
            break;
        case AbilityHeadbutt:
            animation.m_animation = AnimationHeadbutt;
            break;
        case AbilityHornCharge:
            animation.m_animation = AnimationHornCharge;
            break;
        case AbilityTailStrike:
            animation.m_animation = AnimationTailStrike;
            break;
        case AbilityShellBlock:
        case AbilityShellSpin:
            animation.m_animation = AnimationShell;
            break;
        case AbilityScream:
            animation.m_animation = AnimationScream;
            break;
        case AbilityStomp:
            animation.m_animation = AnimationStomp;
            break;
        case AbilityBite:
            animation.m_animation = AnimationClawStrike;
            break;
        case AbilityDashForward:
            animation.m_animation = AnimationDashForward;
            break;
        case AbilityDashLeft:
            animation.m_animation = AnimationDashLeft;
            break;
        case AbilityDashRight:
            animation.m_animation = AnimationDashRight;
            break;
        case AbilitySneak:
            animation.m_animation = AnimationSneak;
            break;
        case AbilityDrink:
            animation.m_animation = AnimationDrink;
            break;
        case AbilityEat:
            animation.m_animation = AnimationEat;
            break;
        case AbilityOfferPetFood:
            animation.m_animation = AnimationOfferPetFood;
            break;
        case AbilitySit:
            animation.m_animation = AnimationSit;
            break;
        case AbilitySleep:
            animation.m_animation = AnimationSleep;
            break;
        case AbilityCyclone:
            animation.m_animation = AnimationCyclone;
            break;
        case AbilityQuake:
            animation.m_animation = AnimationQuake;
            break;
        case Ability93:
            animation.m_animation =
                data.equipment.weaponLeft.category == ItemCategoryNone ? AnimationGuardianCrushStrike : AnimationAmbush;
            break;
        case AbilityShuriken:
            animation.m_animation = AnimationThrow;
            break;
        case AbilityParry:
            animation.m_animation = AnimationParry;
            break;
        case AbilityBulwark:
        case AbilityManaShield:
            animation.m_animation = AnimationBulwark;
            break;
        case AbilityBlockStrike:
            animation.m_animation = AnimationBlockStrike;
            break;
        case AbilityRidePet:
            animation.m_animation = AnimationRide;
            break;
        case AbilitySailBoat:
            animation.m_animation = AnimationSailBoat;
            break;
        case AbilityThrowRock:
            animation.m_animation = AnimationThrowRock;
            break;
        case AbilityPresentItem:
            animation.m_animation = AnimationPresentItem;
            break;
        case AbilityPetFood:
            animation.m_animation = AnimationEatPetFood;
            break;
        default:
            animation.m_animation = AnimationNone;
            break;
        }
        if (data.actionLockTime > 0)
            animation.m_animation = AnimationActionLocked;
        bool hideHead = creature == m_player && isCameraAtFocus();

        // The creature's colour: its light, blue while slowed and tinted by its buffs.
        plasma::Vector<4, float> color(1.0f, 1.0f, 1.0f, light);
        if (data.slowedTime > 0)
            color *= plasma::Vector<4, float>(0.5f, 0.5f, 1.5f, 1.0f);
        for (std::list<Creature::Buff>::iterator buff = creature->m_buffs.begin(); buff != creature->m_buffs.end();
             ++buff) {
            if (buff->type == BuffBulwark || buff->type == BuffWarFrenzy)
                color *= plasma::Vector<4, float>(1.0f, 0.5f, 0.5f, 1.0f);
            // Poison: green, with a ring of rising cubes.
            if (buff->type == BuffPoisoned) {
                color *= plasma::Vector<4, float>(0.5f, 1.5f, 0.5f, 1.0f);
                drawRisingCubeRing(creature->m_drawnPosition - plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                                   0.0f, 0.0f, creature->m_stepOffset)),
                                   plasma::Vector<4, float>(0.0f, 1.0f, 0.0f, 1.0f),
                                   plasma::Vector<4, float>(0.5f, 1.0f, 0.0f, 1.0f), 100000 - buff->timeRemaining, view,
                                   m_projectionMatrix, m_player->m_entityData.appearance.size[0], 0.1f, 8);
            }
            // Buffs 7 and 8: a sign over the head, sized by the buff.
            if (buff->type == BuffTameness) {
                Sprite *sign = m_world.m_spriteManager.sprite(SpriteHeart);
                m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
                plasma::Matrix<float> signModel;
                signModel.identity();
                signModel.translate(
                    (float)(data.position[0] + m_drawOriginX), (float)(data.position[1] + m_drawOriginY),
                    (float)(data.position[2] + data.appearance.size[2] * 0.5f + 0.25f - creature->m_stepOffset));
                signModel.scale(buff->strength * 0.1f + 0.02f, buff->strength * 0.1f + 0.02f,
                                buff->strength * 0.1f + 0.02f);
                signModel.rotateZ(cos(m_world.getClock() * 0.001f) * 40.0f + (m_cameraRotation[2] + 180.0f));
                signModel.translate(sign->sizeX() * -0.5f, sign->sizeY() * -0.5f, 0.0f);
                m_shader->setMatrices(signModel, view, m_projectionMatrix);
                sign->draw();
            }
            if (buff->type == BuffTamingAnger) {
                Sprite *sign = m_world.m_spriteManager.sprite(SpriteAngry);
                m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
                plasma::Matrix<float> signModel;
                signModel.identity();
                signModel.translate(
                    (float)(data.position[0] + m_drawOriginX), (float)(data.position[1] + m_drawOriginY),
                    (float)(data.position[2] + data.appearance.size[2] * 0.5f + 0.25f - creature->m_stepOffset));
                float pulse = cos(m_world.getClock() * 0.005f) * 0.1f + 1.0f;
                float scale = pulse * (data.appearance.size[0] * 0.07f);
                signModel.scale(scale, scale, scale);
                signModel.rotateZ(m_world.getClock() * 0.02f);
                signModel.translate(sign->sizeX() * -0.5f, sign->sizeY() * -0.5f, 0.0f);
                m_shader->setMatrices(signModel, view, m_projectionMatrix);
                sign->draw();
            }
        }
        plasma::Vector<4, float> drawColor = color * tint;
        // AppearanceFlagTinted: lighter in the creature's own colour.
        if (data.appearance.appearanceFlags & (1 << AppearanceFlagTinted))
            drawColor *= 0.5f * (plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f) + creature->color());
        m_shader->setWhite(creature->m_hitFlash);
        // After-images behind a fast creature: the rogue's dashes, abilities 48 and 93, and buffs 12 and
        // 3, fading out over five copies.
        if ((data.classType == ClassRogue && data.specialization == SpecializationNinja &&
             data.timeSinceAbility < creature->attackDuration() &&
             (data.currentAbility == AbilityAmbush || data.currentAbility == AbilityPerforate ||
              data.currentAbility == AbilityKick)) ||
            data.currentAbility == AbilityIntercept || data.currentAbility == Ability93 ||
            creature->findBuff(BuffTempoBoost) || creature->findBuff(BuffCamouflage)) {
            m_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            for (int i = 0; i < 5; i++) {
                float distance = 1.0f - i / 5.0f;
                m_shader->setAlpha((1.0f - distance) * 0.5f);
                plasma::Matrix<float> trailView = view;
                trailView.translate(-distance * data.velocity * 0.07f);
                animation.addSprites(m_shader, trailView, m_projectionMatrix, &m_world.m_spriteManager, 0, drawColor,
                                     m_drawOriginX, m_drawOriginY, hideHead,
                                     m_world.creatureByID(creature->m_companionId));
            }
            m_shader->setAlpha(1.0f);
            m_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        }
        // A creature in stealth is drawn with the translucent ones later.
        if (creature->m_stealth > 0.0f)
            stealthyCreatures.push_back(creature);
        else
            animation.addSprites(m_shader, view, m_projectionMatrix, &m_world.m_spriteManager, m_frameMilliseconds,
                                 drawColor, m_drawOriginX, m_drawOriginY, hideHead,
                                 m_world.creatureByID(creature->m_companionId));
        // The spirit cubes of the equipment, brighter with the combo.
        float combo = (float)data.hitCombo / (float)creature->maxCombo();
        if (animation.m_rightWeaponSprite)
            drawCubes(&data.equipment.weaponRight, animation.m_rightWeaponMatrix, view, m_projectionMatrix, drawColor,
                      combo);
        if (animation.m_leftWeaponSprite)
            drawCubes(&data.equipment.weaponLeft, animation.m_leftWeaponMatrix, view, m_projectionMatrix, drawColor,
                      combo);
        if (animation.m_chestSprite)
            drawCubes(&data.equipment.chest, animation.m_chestMatrix, view, m_projectionMatrix, drawColor, combo);
        if (animation.m_shoulderArmorSprite)
            drawCubes(&data.equipment.shoulder, animation.m_shoulderArmorMatrix, view, m_projectionMatrix, drawColor,
                      combo);
        m_shader->setWhite(0.0f);

        // The stars over a stunned creature's head (sprite 2549), or the sleeper's sign (2551).
        if (data.stunTime > -3000) {
            Sprite *stars = m_world.m_spriteManager.sprite(SpriteStars);
            m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
            plasma::Matrix<float> starsModel;
            starsModel.identity();
            starsModel.translate((float)(data.position[0] + m_drawOriginX), (float)(data.position[1] + m_drawOriginY),
                                 (float)(data.position[2] + data.appearance.size[2] * 0.5f));
            starsModel.scale(data.appearance.size[0] * 0.1f, data.appearance.size[0] * 0.1f,
                             data.appearance.size[0] * 0.1f);
            starsModel.rotateZ((startTime % 360000) * 0.2f);
            starsModel.translate(stars->sizeX() * -0.5f, stars->sizeY() * -0.5f, 0.0f);
            m_shader->setMatrices(starsModel, view, m_projectionMatrix);
            stars->draw();
        } else if (data.currentAbility == AbilitySleep) {
            Sprite *sleep = m_world.m_spriteManager.sprite(SpriteZzz);
            m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
            plasma::Matrix<float> sleepModel;
            sleepModel.identity();
            sleepModel.translate((float)(data.position[0] + m_drawOriginX), (float)(data.position[1] + m_drawOriginY),
                                 (float)(data.position[2] + data.appearance.size[2] * 0.1f));
            float pulse = cos(data.timeSinceAbility * 0.005f) * 0.1f + 1.0f;
            float scale = pulse * (data.appearance.size[0] * 0.07f);
            sleepModel.scale(scale, scale, scale);
            sleepModel.rotateZ(m_cameraRotation[2] + 180.0f + cos(data.timeSinceAbility * 0.001f) * 40.0f);
            m_shader->setMatrices(sleepModel, view, m_projectionMatrix);
            sleep->draw();
        }

        if (data.stunTime <= 0) {
            // The spells 37, 38, 43 and 44 while they are prepared: a spiral of cubes from the head
            // (AppearanceFlagHeadAimOrigin), the left hand (the right weapon id 12 on an odd combo count) or
            // the right hand along the attack direction, growing with the anticipation. Red, or blue for
            // Specialization 1.
            if ((data.currentAbility == AbilityFireball || data.currentAbility == AbilityWandFirebolt ||
                 data.currentAbility == AbilityWaterSplash || data.currentAbility == AbilityWandWaterDrop) &&
                data.timeSinceAbility < creature->anticipationTime()) {
                plasma::Vector<4, float> innerColor(1.0f, 0.0f, 0.0f, 1.0f);
                plasma::Vector<4, float> middleColor(1.0f, 0.25f, 0.0f, 1.0f);
                plasma::Vector<4, float> outerColor(1.0f, 0.5f, 0.0f, 1.0f);
                if (data.specialization == SpecializationWater) {
                    innerColor = plasma::Vector<4, float>(0.0f, 0.0f, 1.0f, 1.0f);
                    middleColor = plasma::Vector<4, float>(0.0f, 0.25f, 1.0f, 1.0f);
                    outerColor = plasma::Vector<4, float>(0.25f, 0.5f, 1.0f, 1.0f);
                }
                float growth = (float)data.timeSinceAbility / (float)creature->anticipationTime();
                if (data.currentAbility == AbilityWandFirebolt)
                    growth *= 0.5f;
                plasma::Vector<3, Fixed> origin = creature->sightPosition();
                if ((data.appearance.appearanceFlags & (1 << AppearanceFlagHeadAimOrigin)) && animation.m_headSprite) {
                    Sprite *head = animation.m_headSprite;
                    origin = plasma::Vector<3, Fixed>(-animation.m_originX, -animation.m_originY, Fixed(0)) +
                             plasma::Vector<3, Fixed>(
                                 animation.m_headMatrix *
                                 (0.5f * plasma::Vector<3, float>((float)head->sizeX(), (float)head->sizeY(),
                                                                  (float)head->sizeZ())));
                } else if (data.equipment.weaponRight.id == WeaponBracelet && data.hitCombo % 2 != 0) {
                    if (animation.m_handsSprite) {
                        Sprite *hands = animation.m_handsSprite;
                        origin = plasma::Vector<3, Fixed>(-animation.m_originX, -animation.m_originY, Fixed(0)) +
                                 plasma::Vector<3, Fixed>(
                                     animation.m_leftHandMatrix *
                                     (0.5f * plasma::Vector<3, float>((float)hands->sizeX(), (float)hands->sizeY(),
                                                                      (float)hands->sizeZ())));
                    } else {
                        origin = plasma::Vector<3, Fixed>(-animation.m_originX, -animation.m_originY, Fixed(0)) +
                                 plasma::Vector<3, Fixed>(animation.m_leftHandMatrix *
                                                          plasma::Vector<3, float>(0.0f, 0.0f, 0.0f));
                    }
                } else if (animation.m_handsSprite) {
                    Sprite *hands = animation.m_handsSprite;
                    origin = plasma::Vector<3, Fixed>(-animation.m_originX, -animation.m_originY, Fixed(0)) +
                             plasma::Vector<3, Fixed>(
                                 animation.m_rightHandMatrix *
                                 (0.5f * plasma::Vector<3, float>((float)hands->sizeX(), (float)hands->sizeY(),
                                                                  (float)hands->sizeZ())));
                }
                drawImpact(origin, data.attackRotation.normalized() * growth, data.rotation, innerColor, middleColor,
                           outerColor, data.timeSinceAbility, view, m_projectionMatrix, 1.0f, growth, 1.0f, 80, false);
            }

            // The buffs that show: rings of cubes around the creature in their colours (buff 6 arcing over
            // it).
            for (std::list<Creature::Buff>::iterator buff = creature->m_buffs.begin(); buff != creature->m_buffs.end();
                 ++buff) {
                plasma::Vector<3, Fixed> feet =
                    creature->m_drawnPosition -
                    plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, creature->m_stepOffset));
                switch (buff->type) {
                case BuffManashield:
                    drawArcingCubeShell(feet, plasma::Vector<4, float>(0.2f, 0.2f, 1.0f, 1.0f),
                                        plasma::Vector<4, float>(0.2f, 0.75f, 1.0f, 1.0f), startTime, view,
                                        m_projectionMatrix, m_player->m_entityData.appearance.size[0] + 0.5f, 0.05f,
                                        40);
                    break;
                case BuffInstantFire:
                    drawRisingCubeRing(feet, plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f),
                                       plasma::Vector<4, float>(1.0f, 1.0f, 0.0f, 1.0f), startTime, view,
                                       m_projectionMatrix, m_player->m_entityData.appearance.size[0], 0.1f, 20);
                    break;
                case BuffInstantShoot:
                    drawRisingCubeRing(feet, plasma::Vector<4, float>(1.0f, 0.0f, 1.0f, 1.0f),
                                       plasma::Vector<4, float>(1.0f, 0.5f, 1.0f, 1.0f), startTime, view,
                                       m_projectionMatrix, m_player->m_entityData.appearance.size[0], 0.1f, 20);
                    break;
                case BuffGuaranteedCritical:
                    drawRisingCubeRing(feet, plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f),
                                       plasma::Vector<4, float>(0.5f, 0.0f, 1.0f, 1.0f), startTime, view,
                                       m_projectionMatrix, m_player->m_entityData.appearance.size[0], 0.1f, 20);
                    break;
                }
            }

            // The abilities 24 and 25: a short pale spiral one block along the attack direction, as wide as
            // half the charge.
            if (data.currentAbility == AbilityChargedShot || data.currentAbility == AbilitySalvo)
                drawImpact(data.position + plasma::Vector<3, Fixed>(data.attackRotation.normalized()),
                           data.attackRotation.normalized(), data.rotation,
                           plasma::Vector<4, float>(0.5f, 0.5f, 1.0f, 1.0f),
                           plasma::Vector<4, float>(0.75f, 0.75f, 1.0f, 1.0f),
                           plasma::Vector<4, float>(0.9f, 0.9f, 1.0f, 1.0f), data.timeSinceAbility, view,
                           m_projectionMatrix, data.charge * 0.5f, data.charge * 0.5f, 1.0f, 20, false);

            // The abilities 87 and 88: four rings in fire colours over the creature, which rise and widen
            // during the anticipation and close again right after it.
            if ((data.currentAbility == AbilityExplosion || data.currentAbility == AbilityFireExplosion) &&
                data.timeSinceAbility < creature->attackDuration()) {
                float rise = (float)data.timeSinceAbility / (float)creature->anticipationTime();
                if (rise > 1.0f) {
                    rise = 20.0f - rise * 19.0f;
                    if (0.0f > rise)
                        rise = 0.0f;
                }
                float height = rise * 4.0f;
                float scale = rise * 0.5f;
                int ring = 0;
                for (int count = 20; count < 72; count += 13) {
                    drawRisingCubeRing(data.position - plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                           0.0f, 0.0f, creature->m_stepOffset + 1.0f + height)),
                                       plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f),
                                       plasma::Vector<4, float>(1.0f, 1.0f, 0.0f, 1.0f), startTime, view,
                                       m_projectionMatrix, ring * 2.0f * height + 1.0f, scale, count);
                    ring++;
                }
            }
        }

        // Ability 101 (a shield, sprite 1067) grows in and out over 0.2 s at the creature's middle.
        if (data.currentAbility == AbilityBulwark && data.timeSinceAbility < creature->attackDuration()) {
            Sprite *shield = m_world.m_spriteManager.sprite(SpriteIronShield04);
            if (shield) {
                plasma::Matrix<float> shieldModel;
                shieldModel.identity();
                shieldModel.translate(plasma::Vector<3, float>(
                    data.position +
                    plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY,
                                             Fixed(data.appearance.size[2] * 0.5f + 0.25f - creature->m_stepOffset))));
                shieldModel.rotate(data.rotation[2], 0.0f, 0.0f, 1.0f);
                float scale = 0.8f / (float)shield->sizeX();
                if (data.timeSinceAbility < 200)
                    scale = data.timeSinceAbility / 200.0f * scale;
                if (data.timeSinceAbility > creature->attackDuration() - 200)
                    scale = (creature->attackDuration() - data.timeSinceAbility) / 200.0f * scale;
                shieldModel.scale(scale, scale, scale);
                shieldModel.translate(shield->sizeX() * -0.5f, shield->sizeY() * -0.5f, 0.0f);
                m_shader->setMatrices(shieldModel, view, m_projectionMatrix);
                m_shader->setMaterialColor(drawColor);
                shield->draw();
            }
        }

        // The sign over a friendly creature's head (hostility type 3): by its class (0x80..0x82, 0x83,
        // 0x89), or the quest mark (sprite 2315) when the region of its home zone holds a mission near the
        // player's level, green until the zone is discovered.
        if (data.hostilityType == HostilityFriendlyNpc) {
            plasma::Vector<4, float> signColor(1.0f, 1.0f, 1.0f, 1.0f);
            Sprite *sign = 0;
            if (data.classType == ClassItemVendor || data.classType == ClassWeaponVendor ||
                data.classType == ClassArmorVendor)
                sign = m_world.m_spriteManager.sprite(SpriteIconVendor);
            if (data.classType == ClassAnalyzer)
                sign = m_world.m_spriteManager.sprite(SpriteIconAnalyze);
            if (data.classType == ClassAdapter)
                sign = m_world.m_spriteManager.sprite(SpriteMission);
            plasma::Vector<3, int> &home = data.questZone;
            if (home[0] >= 0) {
                ZoneTile *tile = m_worldMap.getZoneTile(home[0], home[1]);
                int level = m_player->m_entityData.level;
                Region::Cell *cell = m_world.getCell(home[0], home[1]);
                if (cell && cell->kind && cell->level >= level - QuestLevelRange &&
                    cell->level <= level + QuestLevelRange) {
                    sign = m_world.m_spriteManager.sprite(SpriteIconTalk);
                } else {
                    Region::Record *record =
                        m_world.getRecord(home[0] / ZonesPerSceneryRecord, home[1] / ZonesPerSceneryRecord);
                    if (record && record->kind && record->level >= level - QuestLevelRange &&
                        record->level <= level + QuestLevelRange)
                        sign = m_world.m_spriteManager.sprite(SpriteIconTalk);
                }
                if (!sign)
                    continue;
                if (tile && tile->isDiscovered())
                    signColor = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
                else
                    signColor = plasma::Vector<4, float>(0.25f, 1.0f, 0.25f, 1.0f);
            }
            if (sign) {
                plasma::Matrix<float> signModel;
                signModel.identity();
                signModel.translate(plasma::Vector<3, float>(
                    data.position +
                    plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY,
                                             Fixed(data.appearance.size[2] * 0.5f + 0.25f - creature->m_stepOffset))));
                signModel.rotate(data.rotation[2], 0.0f, 0.0f, 1.0f);
                float scale = 0.8f / (float)sign->sizeX();
                signModel.scale(scale, scale, scale);
                signModel.translate(sign->sizeX() * -0.5f, sign->sizeY() * -0.5f, 0.0f);
                m_shader->setMatrices(signModel, view, m_projectionMatrix);
                m_shader->setMaterialColor(signColor);
                sign->draw();
            }
        }
    }

    // A debugging overlay (m_showPathSearch, never set by the code read so far): every creature's path
    // search as small cubes, blue for the discovered blocks, red for the path, yellow at the goal and
    // green at the start.
    if (m_showPathSearch) {
        for (std::vector<CreatureDistance>::iterator it = creatures.begin(); it != creatures.end(); it++) {
            Creature *creature = it->creature;
            for (std::map<std::vector<int>, Creature::PathNode>::iterator node = creature->m_pathNodes.begin();
                 node != creature->m_pathNodes.end(); ++node) {
                Sprite *cube = m_cubeSprite;
                m_shader->setMaterialColor(plasma::Vector<4, float>(0.2f, 0.7f, 1.0f, 1.0f));
                plasma::Matrix<float> nodeModel;
                nodeModel.identity();
                const std::vector<int> &block = node->first;
                nodeModel.translate((float)(Fixed(block[0]) + 0.5f + m_drawOriginX),
                                    (float)(Fixed(block[1]) + 0.5f + m_drawOriginY), (float)block[2]);
                nodeModel.scale(0.2f, 0.2f, 0.2f);
                nodeModel.translate(cube->sizeX() * -0.5f, cube->sizeY() * -0.5f, 0.0f);
                m_shader->setMatrices(nodeModel, view, m_projectionMatrix);
                cube->draw();
            }
            for (std::list<plasma::Vector<3, int>>::iterator block = creature->m_path.begin();
                 block != creature->m_path.end(); ++block) {
                Sprite *cube = m_cubeSprite;
                m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 0.1f, 0.15f, 1.0f));
                plasma::Matrix<float> blockModel;
                blockModel.identity();
                blockModel.translate((float)(Fixed((*block)[0]) + 0.5f + m_drawOriginX),
                                     (float)(Fixed((*block)[1]) + 0.5f + m_drawOriginY), (float)(*block)[2]);
                blockModel.scale(0.25f, 0.25f, 0.25f);
                blockModel.translate(cube->sizeX() * -0.5f, cube->sizeY() * -0.5f, 0.0f);
                m_shader->setMatrices(blockModel, view, m_projectionMatrix);
                cube->draw();
            }
            if (!creature->m_path.empty()) {
                Sprite *cube = m_cubeSprite;
                m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 0.15f, 1.0f));
                plasma::Matrix<float> goalModel;
                goalModel.identity();
                goalModel.translate((float)(Fixed(toBlock(creature->m_pathGoal[0])) + 0.5f + m_drawOriginX),
                                    (float)(Fixed(toBlock(creature->m_pathGoal[1])) + 0.5f + m_drawOriginY),
                                    (float)toBlock(creature->m_pathGoal[2]));
                goalModel.scale(0.25f, 0.25f, 0.25f);
                goalModel.translate(cube->sizeX() * -0.5f, cube->sizeY() * -0.5f, 0.0f);
                m_shader->setMatrices(goalModel, view, m_projectionMatrix);
                cube->draw();
                m_shader->setMaterialColor(plasma::Vector<4, float>(0.15f, 1.0f, 0.15f, 1.0f));
                plasma::Matrix<float> startModel;
                startModel.identity();
                startModel.translate((float)(Fixed(toBlock(creature->m_pathStart[0])) + 0.5f + m_drawOriginX),
                                     (float)(Fixed(toBlock(creature->m_pathStart[1])) + 0.5f + m_drawOriginY),
                                     (float)toBlock(creature->m_pathStart[2]));
                startModel.scale(0.25f, 0.25f, 0.25f);
                startModel.translate(cube->sizeX() * -0.5f, cube->sizeY() * -0.5f, 0.0f);
                m_shader->setMatrices(startModel, view, m_projectionMatrix);
                cube->draw();
            }
        }
    }

    // The particles: each a cube, turned, sized and transformed by its matrix; types 0 and 4 take the
    // light of their block (at least 0.2) and fade, the others are opaque.
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    for (std::list<Particle>::iterator particle = m_particles.begin(); particle != m_particles.end(); ++particle) {
        if (!isSphereVisible(particle->position, particle->size, effectRange))
            continue;
        float brightness = 1.0f;
        if (particle->type == ParticleLitCube || particle->type == ParticleLeaf) {
            brightness = m_world.cubeLight((int)particle->position[0], (int)particle->position[1],
                                           (int)particle->position[2], 0, 0) /
                         255.0f;
            if (0.2f > brightness)
                brightness = 0.2f;
        }
        plasma::Vector<4, float> particleColor =
            particle->color * plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, brightness) * tint;
        if (particle->type != ParticleLitCube && particle->type != ParticleLeaf)
            particleColor[3] = 1.0f;
        m_shader->setMaterialColor(particleColor);
        plasma::Matrix<float> particleModel;
        particleModel.identity();
        particleModel.translate(plasma::Vector<3, float>(
            particle->position + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0))));
        particleModel.rotate(particle->rotation[2], 0.0f, 0.0f, 1.0f);
        particleModel.rotate(particle->rotation[1], 0.0f, 1.0f, 0.0f);
        particleModel.rotate(particle->rotation[0], 1.0f, 0.0f, 0.0f);
        particleModel.scale(particle->size, particle->size, particle->size);
        particleModel *= particle->matrix;
        particleModel.translate(-0.5f, -0.5f, -0.5f);
        m_shader->setMatrices(particleModel, view, m_projectionMatrix);
        m_cubeSprite->draw();
    }
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);

    // The projectiles: a sprite by type, turned along the flight, and the effects of the types that
    // have some.
    for (std::list<BulletPacket>::iterator projectile = m_world.m_projectiles.begin();
         projectile != m_world.m_projectiles.end(); ++projectile) {
        if (!isSphereVisible(projectile->position, projectile->sizeInBlocks, plantFadeEnd))
            continue;
        Sprite *sprite;
        switch (projectile->projectileType) {
        case ProjectileArrow:
            sprite = m_world.m_spriteManager.sprite(SpriteArrow);
            // A pale spiral behind an arrow with knockback.
            if (projectile->knockback != 0.0f)
                drawImpact(projectile->position + plasma::Vector<3, Fixed>(projectile->velocity.normalized()),
                           projectile->velocity.normalized(), plasma::Vector<3, float>(0.0f, 0.0f, 0.0f),
                           plasma::Vector<4, float>(0.5f, 0.5f, 1.0f, 1.0f),
                           plasma::Vector<4, float>(0.75f, 0.75f, 1.0f, 1.0f),
                           plasma::Vector<4, float>(0.9f, 0.9f, 1.0f, 1.0f), projectile->currentTime, view,
                           m_projectionMatrix, projectile->knockback * 0.5f, projectile->knockback, 1.0f, 20, false);
            break;
        case ProjectileBoomerang: {
            // The owner's weapon when that is a boomerang.
            Creature *owner = m_world.creatureByID(projectile->ownerId);
            if (owner &&
                m_world.creatureByID(projectile->ownerId)->m_entityData.equipment.weaponRight.category ==
                    ItemCategoryWeapon &&
                m_world.creatureByID(projectile->ownerId)->m_entityData.equipment.weaponRight.id == WeaponBoomerang)
                sprite = m_world.m_spriteManager.itemSprite(
                    &m_world.creatureByID(projectile->ownerId)->m_entityData.equipment.weaponRight);
            else
                sprite = m_world.m_spriteManager.sprite(SpriteArrow);
            break;
        }
        case ProjectileRock:
            sprite = m_world.m_spriteManager.sprite(SpriteIronDeposit);
            break;
        case ProjectileMagic: {
            // A spiral six times its size along the flight, in fire colours or, for an owner of specialization
            // 1, in blue; no sprite.
            plasma::Vector<3, float> along = projectile->velocity.normalized() * projectile->visualSize * 6.0f;
            plasma::Vector<4, float> innerColor(1.0f, 0.0f, 0.0f, 1.0f);
            plasma::Vector<4, float> middleColor(1.0f, 0.25f, 0.0f, 1.0f);
            plasma::Vector<4, float> outerColor(1.0f, 0.5f, 0.0f, 1.0f);
            if (m_world.creatureByID(projectile->ownerId) &&
                m_world.creatureByID(projectile->ownerId)->m_entityData.specialization == SpecializationWater) {
                innerColor = plasma::Vector<4, float>(0.0f, 0.0f, 1.0f, 1.0f);
                middleColor = plasma::Vector<4, float>(0.0f, 0.25f, 1.0f, 1.0f);
                outerColor = plasma::Vector<4, float>(0.25f, 0.5f, 1.0f, 1.0f);
            }
            drawImpact(projectile->position - plasma::Vector<3, Fixed>(along * 0.5f), along,
                       plasma::Vector<3, float>(0.0f, 0.0f, 0.0f), innerColor, middleColor, outerColor,
                       projectile->currentTime, view, m_projectionMatrix, 0.1f, projectile->visualSize * 5.0f, 0.75f,
                       20, false);
            continue;
        }
        default:
            continue;
        }
        if (!sprite)
            continue;
        // Magic glows; the others take the light of their block.
        float light = 1.0f;
        if (projectile->projectileType != ProjectileMagic)
            light = m_world.cubeLight((int)projectile->position[0], (int)projectile->position[1],
                                      (int)projectile->position[2], 0, 0) /
                    255.0f;
        m_shader->setMaterialColor(tint * plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, light));
        plasma::Matrix<float> projectileModel;
        projectileModel.identity();
        projectileModel.translate(plasma::Vector<3, float>(
            projectile->position + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0))));
        // The boomerang spins flat, type 4 rolls across its flight, and the others turn their +Y axis onto
        // the direction they fly.
        if (projectile->projectileType == ProjectileBoomerang) {
            float tilt = projectile->currentTime * 0.5f;
            if (tilt > 90.0f)
                tilt = 90.0f;
            projectileModel.rotateY(tilt);
            projectileModel.rotateX(-projectile->currentTime * 1.5f);
        } else if (projectile->projectileType == ProjectileRock) {
            projectileModel.rotate(projectile->currentTime * 0.5f,
                                   projectile->velocity.cross(plasma::Vector<3, float>(0.0f, 0.0f, 1.0f)));
        } else if (projectile->velocity.squaredLength() > 0.0f) {
            projectileModel.rotateTowards(plasma::Vector<3, float>(0.0f, 1.0f, 0.0f),
                                          projectile->velocity.normalized());
        }
        projectileModel.scale(projectile->visualSize / 14.0f, projectile->visualSize / 14.0f,
                              projectile->visualSize / 14.0f);
        projectileModel.translate(sprite->sizeX() * -0.5f, sprite->sizeY() * -0.5f, sprite->sizeZ() * -0.5f);
        m_shader->setMatrices(projectileModel, view, m_projectionMatrix);
        sprite->draw();
    }

    // Draws cloud spots and round creature shadows through the fixed-function pipeline, placing a textured quad on the first solid block below each column.
    m_shader->setMaterialColor(tint);
    m_device->SetPixelShader(0);
    m_device->SetVertexShader(0);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    m_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    m_device->SetTransform(D3DTS_PROJECTION, (const D3DMATRIX *)&m_projectionMatrix);
    m_device->SetTransform(D3DTS_VIEW, (const D3DMATRIX *)&view);
    m_device->SetTexture(0, m_shadowTexture);
    m_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
    m_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
    for (std::list<BulletPacket>::iterator projectile = m_world.m_projectiles.begin();
         projectile != m_world.m_projectiles.end(); ++projectile) {
        if (projectile->projectileType != ProjectileCloud ||
            !isSphereVisible(projectile->position, projectile->sizeInBlocks, entityFadeEnd))
            continue;
        plasma::Vector<3, int> center(projectile->position);
        float size = projectile->sizeInBlocks;
        float step = 1.0f / (size * 2.0f);
        int pulse = (int)(cos(projectile->currentTime * 0.01f) * -50.0f);
        D3DCOLOR markColor = D3DCOLOR_ARGB(255, 0, 150 - pulse, 255);
        if (projectile->element == ElementFire)
            markColor = D3DCOLOR_ARGB(255, 255, 50 - pulse, 0);
        int extent = (int)(size + 1.0f);
        for (int x = -extent; x <= extent; x++) {
            for (int y = -extent; y <= extent; y++) {
                int z = center[2];
                int depth = 0;
                bool found = true;
                while (!m_world.getBlock(center[0] + x, center[1] + y, z, 0).isSolid()) {
                    depth++;
                    z--;
                    if (depth > GroundSearchDepth) {
                        found = false;
                        break;
                    }
                }
                if (!found)
                    continue;
                z++;
                float u = ((float)x + size - (float)(projectile->position[0] - center[0])) * step;
                float v = ((float)y + size - (float)(projectile->position[1] - center[1])) * step;
                SunVertex quad[4] = {
                    {plasma::Vector<3, float>((float)x, (float)y, (float)z), markColor, plasma::Vector<2, float>(u, v)},
                    {plasma::Vector<3, float>((float)(x + 1), (float)y, (float)z), markColor,
                     plasma::Vector<2, float>(u + step, v)},
                    {plasma::Vector<3, float>((float)(x + 1), (float)(y + 1), (float)z), markColor,
                     plasma::Vector<2, float>(u + step, v + step)},
                    {plasma::Vector<3, float>((float)x, (float)(y + 1), (float)z), markColor,
                     plasma::Vector<2, float>(u, v + step)},
                };
                plasma::Matrix<float> markModel;
                markModel.identity();
                markModel.translate((float)(Fixed(center[0]) + m_drawOriginX),
                                    (float)(Fixed(center[1]) + m_drawOriginY), 0.01f);
                m_device->SetTransform(D3DTS_WORLD, (const D3DMATRIX *)&markModel);
                m_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad, sizeof(SunVertex));
            }
        }
    }
    if (!m_startScreen->isVisible() && !m_characterListScreen->isVisible() && !m_worldListScreen->isVisible() &&
        !m_connectScreen->isVisible()) {
        for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin();
             it != m_world.m_creatures.end(); ++it) {
            Creature *creature = it->second;
            if (!creature || !(creature->m_entityData.hp > 0.0f) ||
                !isSphereVisible(creature->m_drawnPosition, creature->m_entityData.appearance.size[2],
                                 creatureRange * 0.5f))
                continue;
            if (m_drawPlayerOnly && creature != m_player)
                continue;
            plasma::Matrix<float> turn;
            turn.identity();
            turn.rotateZ(creature->m_entityData.rotation[2]);
            plasma::Vector<3, float> offset = turn.transformDirection(creature->m_animationState.m_creatureOffset);
            plasma::Vector<3, int> center(creature->m_drawnPosition + plasma::Vector<3, Fixed>(offset));
            for (int x = -2; x <= 2; x++) {
                for (int y = -2; y <= 2; y++) {
                    int z = center[2];
                    int depth = 0;
                    bool found = true;
                    while (!m_world.getBlock(center[0] + x, center[1] + y, z, 0).isSolid()) {
                        depth++;
                        z--;
                        if (depth > GroundSearchDepth) {
                            found = false;
                            break;
                        }
                    }
                    if (!found)
                        continue;
                    z++;
                    float u = ((float)x + 1.5f - (float)(creature->m_drawnPosition[0] + offset[0] - center[0])) *
                              (1.0f / 3.0f);
                    float v = ((float)y + 1.5f - (float)(creature->m_drawnPosition[1] + offset[1] - center[1])) *
                              (1.0f / 3.0f);
                    SunVertex quad[4] = {
                        {plasma::Vector<3, float>((float)x, (float)y, (float)z), 0xff000000,
                         plasma::Vector<2, float>(u, v)},
                        {plasma::Vector<3, float>((float)(x + 1), (float)y, (float)z), 0xff000000,
                         plasma::Vector<2, float>(u + 1.0f / 3.0f, v)},
                        {plasma::Vector<3, float>((float)(x + 1), (float)(y + 1), (float)z), 0xff000000,
                         plasma::Vector<2, float>(u + 1.0f / 3.0f, v + 1.0f / 3.0f)},
                        {plasma::Vector<3, float>((float)x, (float)(y + 1), (float)z), 0xff000000,
                         plasma::Vector<2, float>(u, v + 1.0f / 3.0f)},
                    };
                    plasma::Matrix<float> shadowModel;
                    shadowModel.identity();
                    shadowModel.translate((float)(Fixed(center[0]) + m_drawOriginX),
                                          (float)(Fixed(center[1]) + m_drawOriginY), 0.02f);
                    m_device->SetTransform(D3DTS_WORLD, (const D3DMATRIX *)&shadowModel);
                    m_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad, sizeof(SunVertex));
                }
            }
        }
    }

    // The weapon trails of the creatures attacking in melee: a strip between two of the animation's
    // trail point rows, brightest in the middle of its length.
    m_device->SetTexture(0, 0);
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         ++it) {
        Creature *creature = it->second;
        if (!creature || !(creature->m_entityData.hp > 0.0f) ||
            !isSphereVisible(creature->m_drawnPosition, creature->m_entityData.appearance.size[1], creatureRange) ||
            creature->isAttackingFromRange() || creature->m_entityData.timeSinceAbility > creature->attackDuration())
            continue;
        plasma::Matrix<float> trailModel;
        trailModel.identity();
        m_device->SetTransform(D3DTS_VIEW, (const D3DMATRIX *)&view);
        m_device->SetTransform(D3DTS_WORLD, (const D3DMATRIX *)&trailModel);
        StarVertex strip[32];
        plasma::Vector<3, float> shift((float)(-creature->m_drawnPosition[0] - m_drawOriginX),
                                       (float)(-creature->m_drawnPosition[1] - m_drawOriginY), 0.0f);
        // The colours by the combo (both replaced by white afterwards).
        float glow = creature->m_entityData.hitCombo * 0.1f;
        if (glow > 1.0f)
            glow = 1.0f;
        float rest = 1.0f - glow;
        plasma::Vector<3, unsigned char> innerColor(glow * plasma::Vector<3, float>(200.0f, 100.0f, 255.0f) +
                                                    rest * plasma::Vector<3, float>(100.0f, 255.0f, 255.0f));
        plasma::Vector<3, unsigned char> outerColor(glow * plasma::Vector<3, float>(255.0f, 50.0f, 100.0f) +
                                                    rest * plasma::Vector<3, float>(255.0f, 255.0f, 255.0f));
        outerColor = plasma::Vector<3, unsigned char>(plasma::Vector<3, float>(255.0f, 255.0f, 255.0f));
        innerColor = plasma::Vector<3, unsigned char>(plasma::Vector<3, float>(255.0f, 255.0f, 255.0f));
        Creature::AnimationState &animation = creature->m_animationState;
        for (int i = 0; i < 16; i++) {
            float along = 1.0f - i / 15.0f;
            float middle = (along - 0.5f) * 2.0f;
            float alpha = (1.0f - middle * middle) * (1.0f - middle * middle);
            strip[i * 2].position = animation.m_trails[1][i] - shift;
            strip[i * 2].color = D3DCOLOR_ARGB((int)(alpha * 50.0f), innerColor[0], innerColor[1], innerColor[2]);
            strip[i * 2 + 1].position = animation.m_trails[0][i] - shift;
            strip[i * 2 + 1].color = D3DCOLOR_ARGB((int)(alpha * 200.0f), outerColor[0], outerColor[1], outerColor[2]);
        }
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 30, strip, sizeof(StarVertex));
        // The second weapon's trail.
        if (creature->m_entityData.equipment.weaponLeft.category == ItemCategoryWeapon) {
            for (int i = 0; i < 16; i++) {
                float along = 1.0f - i / 15.0f;
                float middle = (along - 0.5f) * 2.0f;
                float alpha = (1.0f - middle * middle) * (1.0f - middle * middle);
                strip[i * 2].position = animation.m_trails[3][i] - shift;
                strip[i * 2].color = D3DCOLOR_ARGB((int)(alpha * 50.0f), innerColor[0], innerColor[1], innerColor[2]);
                strip[i * 2 + 1].position = animation.m_trails[2][i] - shift;
                strip[i * 2 + 1].color =
                    D3DCOLOR_ARGB((int)(alpha * 255.0f), outerColor[0], outerColor[1], outerColor[2]);
            }
            m_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 30, strip, sizeof(StarVertex));
        }
    }

    // The water faces of the chunks, farthest first.
    m_shader->setAlpha(1.0f);
    m_shader->applyWater();
    m_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_shader->setMaterialColor(tint);
    for (std::vector<Chunk *>::reverse_iterator it = visibleChunks.rbegin(); it != visibleChunks.rend(); it++) {
        Chunk *chunk = *it;
        EnterCriticalSection(&chunk->lock);
        for (std::list<ChunkBuffer *>::iterator buffer = chunk->chunkBuffers.begin();
             buffer != chunk->chunkBuffers.end(); buffer++) {
            if (!(*buffer)->waterIndexBuffer)
                continue;
            plasma::Matrix<float> waterModel;
            waterModel.identity();
            waterModel.translate((float)(Fixed(chunk->position[0] << 5) + m_drawOriginX),
                                 (float)(Fixed(chunk->position[1] << 5) + m_drawOriginY), (float)(*buffer)->baseZ);
            m_shader->setPointLights(chunk->lightPositions, chunk->lightColors);
            m_shader->setMatrices(waterModel, view, m_projectionMatrix);
            m_device->SetStreamSource(0, (*buffer)->vertexBuffer, 0, sizeof(Vertex));
            m_device->SetIndices((*buffer)->waterIndexBuffer);
            m_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, (*buffer)->vertexCount, 0,
                                           (*buffer)->waterPrimitiveCount);
        }
        LeaveCriticalSection(&chunk->lock);
    }
    m_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    m_shader->apply();

    // The plants and objects beyond their fade distance, farthest first.
    for (std::vector<DeferredSprite>::iterator it = deferred.begin(); it != deferred.end(); it++) {
        if (it->plant)
            renderPlant(*it->plant, it->distanceSquared, plantFadeEnd, plantFadeStart, it->index, view,
                        m_projectionMatrix, tint);
        if (it->object)
            renderStaticEntity(*it->object, it->distanceSquared, entityFadeEnd, entityFadeStart, view,
                               m_projectionMatrix, tint);
    }

    // The creatures in stealth: their depth first, then translucent by how hidden they are.
    for (std::list<Creature *>::iterator it = stealthyCreatures.begin(); it != stealthyCreatures.end(); ++it) {
        Creature *creature = *it;
        Creature::EntityData &data = creature->m_entityData;
        float light =
            m_world.cubeLight((int)data.position[0], (int)data.position[1], (int)(data.position[2] - 0.1f), 0, 0) /
            255.0f;
        bool hideHead = creature == m_player && isCameraAtFocus();
        plasma::Vector<4, float> color(1.0f, 1.0f, 1.0f, light);
        if (data.slowedTime > 0)
            color *= plasma::Vector<4, float>(0.5f, 0.5f, 1.5f, 1.0f);
        for (std::list<Creature::Buff>::iterator buff = creature->m_buffs.begin(); buff != creature->m_buffs.end();
             ++buff) {
            if (buff->type == BuffBulwark)
                color *= plasma::Vector<4, float>(1.0f, 0.5f, 0.5f, 1.0f);
        }
        plasma::Vector<4, float> drawColor = color * tint;
        if (data.appearance.appearanceFlags & (1 << AppearanceFlagTinted))
            drawColor *= 0.5f * (plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f) + creature->color());
        m_shader->setWhite(creature->m_hitFlash);
        if (creature->m_stealth > 0.0f) {
            m_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            creature->m_animationState.addSprites(m_shader, view, m_projectionMatrix, &m_world.m_spriteManager,
                                                  m_frameMilliseconds, drawColor, m_drawOriginX, m_drawOriginY,
                                                  hideHead, m_world.creatureByID(creature->m_companionId));
            m_device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                                                                 D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
            m_shader->setAlpha(1.0f - creature->m_stealth * 0.75f);
            creature->m_animationState.addSprites(m_shader, view, m_projectionMatrix, &m_world.m_spriteManager, 0,
                                                  drawColor, m_drawOriginX, m_drawOriginY, hideHead,
                                                  m_world.creatureByID(creature->m_companionId));
            m_shader->setAlpha(1.0f);
        }
    }
    m_shader->setWhite(0.0f);
    m_shader->apply();
    m_shader->setAlpha(1.0f);
    m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));

    // The interface: the minimap and the ring menu under the game screen's nodes, then the nodes, then
    // the portraits over them.
    m_minimapNode->destroyChildren();
    m_device->Clear(0, 0, D3DCLEAR_ZBUFFER, 0xffffffff, 1.0f, 0);
    if (m_gameScreen->isVisible() && m_engine->root()->isVisible()) {
        // The minimap in the bottom right corner, zoomed in three steps.
        int width = m_windowWidth;
        int height = m_windowHeight;
        if (0.35f > m_mapZoom)
            m_worldMap.render(m_player->m_entityData.position, width - 240, height - 170, width, height,
                              plasma::Vector<3, float>(-120.0f, 0.0f, -m_cameraRotation[2]), 0.025f,
                              m_frameMilliseconds, 0, false);
        else if (m_mapZoom >= 2.0f)
            m_worldMap.render(m_player->m_entityData.position, width - 240, height - 170, width, height,
                              plasma::Vector<3, float>(-120.0f, 0.0f, -m_cameraRotation[2]), 1.6f, m_frameMilliseconds,
                              0, false);
        else
            m_worldMap.render(m_player->m_entityData.position, width - 240, height - 170, width, height,
                              plasma::Vector<3, float>(-120.0f, 0.0f, -m_cameraRotation[2]), 0.8f, m_frameMilliseconds,
                              1, false);
        m_shader->setLight(
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), plasma::Vector<4, float>(0.2f, 0.3f, 0.4f, 1.0f),
            plasma::Vector<3, float>(0.5f, 0.4f, -0.6f).normalized(), plasma::Vector<4, float>(0.4f, 0.4f, 0.4f, 1.0f));
        // The ring menu (the usable items, inventory tab and slot each) on a circle around the middle of
        // the window, opening to a radius of 200 and turning to the selected entry.
        std::vector<plasma::Vector<2, int>> usable = collectUsableItems();
        if (m_ringMenuOpen) {
            blend(m_ringRadius, 200.0f, m_frameMilliseconds, 0.01f);
            int count = (int)usable.size();
            blend(m_ringRotation, m_ringMenuSelection * -360.0f / count, m_frameMilliseconds, 0.01f);
            m_device->SetRenderState(D3DRS_ZENABLE, TRUE);
            m_shader->apply();
            plasma::Vector<4, float> menuLightPositions[16];
            plasma::Vector<3, float> menuLightColors[16];
            for (int i = 0; i < 16; i++) {
                menuLightPositions[i] = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f);
                menuLightColors[i] = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
            }
            m_shader->setPointLights(menuLightPositions, menuLightColors);
            m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
            m_shader->setAlpha(1.0f);
            m_shader->setShininess(0.0f);
            for (int i = 0; i < count; i++) {
                float angle = (float)(i * 6.283185307179586 / count + m_ringRotation * 3.141592653589793 / 180.0 -
                                      1.5707963267948966);
                plasma::Vector<2, float> position(cos(angle) * m_ringRadius + m_windowWidth * 0.5f,
                                                  sin(angle) * m_ringRadius + m_windowHeight * 0.5f);
                ItemStack &stack = m_player->m_inventory.m_inventoryTabs[usable[i][0]][usable[i][1]];
                drawItem(position[0], position[1], m_previewRotation, 0.09f, &stack.item, 0.0f);
            }
        } else {
            m_ringRadius = 0.0f;
        }
        m_device->SetTransform(D3DTS_WORLD, (const D3DMATRIX *)&identity);
    }
    m_engine->drawWindow();
    if (m_gameScreen->isVisible() && m_engine->root()->isVisible()) {
        m_device->SetRenderState(D3DRS_ZENABLE, TRUE);
        m_shader->apply();
        m_device->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
        // The player's head in the top left corner, swaying.
        plasma::Vector<3, float> sway(cos((float)(m_engine->getCaretClock() * 0.0005)) * 20.0f, 0.0f,
                                      sin((float)(m_engine->getCaretClock() * 0.001)) * 20.0f);
        m_shader->setLight(
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), plasma::Vector<4, float>(0.2f, 0.3f, 0.4f, 1.0f),
            plasma::Vector<3, float>(0.0f, 0.5f, -1.0f).normalized(), plasma::Vector<4, float>(0.4f, 0.4f, 0.4f, 1.0f));
        plasma::Vector<4, float> portraitLightPositions[16];
        plasma::Vector<3, float> portraitLightColors[16];
        for (int i = 0; i < 16; i++) {
            portraitLightPositions[i] = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f);
            portraitLightColors[i] = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        }
        m_shader->setPointLights(portraitLightPositions, portraitLightColors);
        m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
        Creature::Appearance &appearance = m_player->m_entityData.appearance;
        drawSpriteModel(50.0f, 60.0f, plasma::Vector<3, float>(-120.0f, 0.0f, 20.0f) + sway, 0.0035f,
                        m_world.m_spriteManager.sprite(appearance.headModel), 0.0f);
        m_shader->setMaterialColor(plasma::Vector<4, float>(appearance.hairColor[0] / 255.0f,
                                                            appearance.hairColor[1] / 255.0f,
                                                            appearance.hairColor[2] / 255.0f, 1.0f));
        drawSpriteModel(50.0f, 60.0f, plasma::Vector<3, float>(-120.0f, 0.0f, 20.0f) + sway, 0.0035f,
                        m_world.m_spriteManager.sprite(appearance.hairModel), 0.0f);
        // The companion's head (or its body) below it.
        Creature *companion = m_world.creatureByID(m_player->m_companionId);
        if (companion) {
            m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
            if (companion->m_entityData.appearance.headModel >= 0)
                drawSpriteModel(50.0f, 210.0f, plasma::Vector<3, float>(-140.0f, 0.0f, 20.0f) + sway, 0.0035f,
                                m_world.m_spriteManager.sprite(companion->m_entityData.appearance.headModel), 0.0f);
            else if (companion->m_entityData.appearance.chestModel >= 0)
                drawSpriteModel(50.0f, 210.0f, plasma::Vector<3, float>(-140.0f, 0.0f, 20.0f) + sway, 0.0035f,
                                m_world.m_spriteManager.sprite(companion->m_entityData.appearance.chestModel), 0.0f);
        }
        // The compass in the top right corner.
        m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
        drawSpriteModel((float)(m_windowWidth - 60), 40.0f,
                        plasma::Vector<3, float>(-120.0f, 0.0f, -m_cameraRotation[2]), 0.003f,
                        m_world.m_spriteManager.sprite(SpriteCompass2), 0.0f);
        // The other players' heads along the top, by the party's life bars, each swaying out of step.
        int member = 0;
        int phase = 0;
        int x = 270;
        for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin();
             it != m_world.m_creatures.end(); ++it) {
            Creature *creature = it->second;
            if (!creature)
                continue;
            if (member >= (int)m_partyLifeBars.size())
                break;
            if (creature->m_entityData.hostilityType != HostilityPlayer || creature == m_player)
                continue;
            plasma::Vector<3, float> memberSway(cos((float)(m_engine->getCaretClock() * 0.0005 + phase)) * 20.0f, 0.0f,
                                                sin((float)(m_engine->getCaretClock() * 0.001 + phase)) * 20.0f);
            m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
            Creature::Appearance &look = creature->m_entityData.appearance;
            drawSpriteModel((float)x, 45.0f, plasma::Vector<3, float>(-120.0f, 0.0f, 20.0f) + memberSway, 0.0025f,
                            m_world.m_spriteManager.sprite(look.headModel), 0.0f);
            m_shader->setMaterialColor(plasma::Vector<4, float>(look.hairColor[0] / 255.0f, look.hairColor[1] / 255.0f,
                                                                look.hairColor[2] / 255.0f, 1.0f));
            drawSpriteModel((float)x, 45.0f, plasma::Vector<3, float>(-120.0f, 0.0f, 20.0f) + memberSway, 0.0025f,
                            m_world.m_spriteManager.sprite(look.hairModel), 0.0f);
            member++;
            phase += 43;
            x += 220;
        }
        m_shader->setMaterialColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
        // The previews of the inventory screens turn.
        m_previewRotation[0] = 225.0f;
        m_previewRotation[1] = 0.0f;
        m_previewRotation[2] += m_frameMilliseconds * 0.002f * 5.0f * 3.0f;
    }

    m_device->EndScene();
    m_world.leaveRegions();
}

// The cube effects' shapes are hand-set: each cube's two-second cycle starts 443 milliseconds
// after the previous one's; it pulses (13 radians apart, half a second a cycle), ten cubes go
// round a turn, which takes ten seconds, and the cube tumbles 40, 30 and 10 degrees about x, y and
// z over its cycle.
void GameController::drawRisingCubeRing(const plasma::Vector<3, Fixed> &position,
                                        const plasma::Vector<4, float> &startColor,
                                        const plasma::Vector<4, float> &endColor, int time,
                                        const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection,
                                        float radius, float scale, int count)
{
    for (int i = 0; i < count; i++) {
        float progress = (float)((time + i * 443) % 2000) / 2000.0f;
        float fade = (progress - 0.5f) * 2.0f;
        float pulse = cos((float)(i * 13 + (float)(time % 500) / 500.0f * 2.0f * 3.141592653589793));
        float size = (pulse * 0.5f + 1.0f) * scale * (1.0f - fade * fade);
        float angle = (float)(((float)i / 10.0f + (float)(time % 10000) * 0.0001f) * 2.0f * 3.141592653589793);
        m_shader->setMaterialColor(startColor * (1.0f - progress) + endColor * progress);

        plasma::Matrix<float> model;
        model.identity();
        model.translate(plasma::Vector<3, float>(cos(angle) * radius, sin(angle) * radius, progress * radius * 2.0f));
        model.translate((float)(position[0] + m_drawOriginX), (float)(position[1] + m_drawOriginY), (float)position[2]);
        model.rotateX(progress * 40.0f);
        model.rotateY(progress * 30.0f);
        model.rotateZ(progress * 10.0f);
        model.scale(size, size, size);
        model.translate((float)m_cubeSprite->sizeX() * -0.5f, (float)m_cubeSprite->sizeY() * -0.5f,
                        (float)m_cubeSprite->sizeZ() * -0.5f);
        m_shader->setMatrices(model, view, projection);
        m_cubeSprite->draw();
    }
}

// The shape is drawRisingCubeRing's, the cubes spiralling out (two turns).
void GameController::drawExpandingCubeSpiral(const plasma::Vector<3, Fixed> &position,
                                             const plasma::Vector<4, float> &startColor,
                                             const plasma::Vector<4, float> &endColor, int time,
                                             const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection,
                                             float radius, float scale, float turns, int count)
{
    for (int i = 0; i < count; i++) {
        float progress = (float)((time + i * 443) % 2000) / 2000.0f;
        float fade = (progress - 0.5f) * 2.0f;
        float pulse = cos((float)(i * 13 + (float)(time % 500) / 500.0f * 2.0f * 3.141592653589793));
        float size = (pulse * 0.5f + 1.0f) * scale * (1.0f - fade * fade);
        float angle = (float)(progress * progress * 4.0f * 3.141592653589793 * turns +
                              ((float)i / 10.0f + (float)(time % 10000) * 0.0001f) * 2.0f * 3.141592653589793);
        m_shader->setMaterialColor(startColor * (1.0f - progress) + endColor * progress);

        float distance = progress * radius;
        plasma::Matrix<float> model;
        model.identity();
        model.translate(plasma::Vector<3, float>(cos(angle) * distance, sin(angle) * distance, distance * 2.0f));
        model.translate((float)(m_drawOriginX + position[0]), (float)(m_drawOriginY + position[1]), (float)position[2]);
        model.rotateX(progress * 40.0f);
        model.rotateY(progress * 30.0f);
        model.rotateZ(progress * 10.0f);
        model.scale(size, size, size);
        model.translate((float)m_cubeSprite->sizeX() * -0.5f, (float)m_cubeSprite->sizeY() * -0.5f,
                        (float)m_cubeSprite->sizeZ() * -0.5f);
        m_shader->setMatrices(model, view, projection);
        m_cubeSprite->draw();
    }
}

// The sway is hand-set: two cosines per axis of the world clock (0.004 and 0.0027 radians per
// millisecond, 84 and 93 radians on, 30 per plant).
void GameController::renderPlant(const Plant &plant, float distanceSquared, float fadeEnd, float fadeStart, int index,
                                 const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection,
                                 const plasma::Vector<4, float> &tint)
{
    Sprite *sprite = m_plantSprites[plant.type];
    if (sprite) {
        plasma::Matrix<float> model;
        model.identity();
        model.translate((float)(m_drawOriginX + plant.position[0]), (float)(m_drawOriginY + plant.position[1]),
                        (float)plant.position[2]);
        model.scale(plant.scale, plant.scale, plant.scale);
        // Flag 4: the top sways with the clock (a shear applied to a model a tenth of a block high), each
        // plant a little out of step.
        if (plant.flags & (1 << PlantFlagSway)) {
            float flat = 0.1f / ((float)sprite->sizeZ() * plant.scale);
            model.rotateY(-90.0f);
            plasma::Matrix<float> shearX;
            shearX.identity();
            shearX[1] =
                (cos(m_world.m_clock * 0.004f + (float)(index * 30) + 84.0f) + cos(m_world.m_clock * 0.0027f + 93.0f)) *
                flat;
            model *= shearX;
            model.rotateY(90.0f);
            model.rotateX(-90.0f);
            plasma::Matrix<float> shearY;
            shearY.identity();
            shearY[4] =
                (cos(m_world.m_clock * 0.005f + (float)(index * 30)) + cos(m_world.m_clock * 0.003f)) * (flat * 0.8f);
            model *= shearY;
            model.rotateX(90.0f);
        }
        model.rotateZ(plant.rotation);
        model.translate((float)sprite->sizeX() * -0.5f, (float)sprite->sizeY() * -0.5f, 0.0f);

        m_shader->setAlpha(distanceSquared > fadeStart * fadeStart
                               ? 1.0f - (sqrt(distanceSquared) - fadeStart) / (fadeEnd - fadeStart)
                               : 1.0f);
        m_shader->setMatrices(model, view, projection);
        // Flag 1: it shines; otherwise it takes the light stored with it.
        plasma::Vector<4, float> color = tint;
        if (plant.flags & (1 << PlantFlagEmissive))
            color[3] = 1.0f;
        else
            color[3] = plant.daylight / 255.0f * tint[3];
        m_shader->setMaterialColor(color);
        sprite->draw();
    }
}

// How far a static entity that opens (a door, a gate, a chest's lid) is open, 0..1: it closes at
// closingRate per millisecond while active is set and opens at 0.001 while it is clear (openness).
static float openness(const Zone::ObjectState &entity, float closingRate)
{
    if (entity.active)
        return std::max(0.0f, 1.0f - entity.transitionTime * closingRate);
    return std::min(1.0f, entity.transitionTime * 0.001f);
}

// The objects open at the rates given to openness (per millisecond: a gate and a chest 0.001, a
// trap 0.01); the chest's lid turns about a hinge 9 sprite units up and 1 back.
void GameController::renderStaticEntity(const Zone::ObjectState &entity, float distanceSquared, float fadeEnd,
                                        float fadeStart, const plasma::Matrix<float> &view,
                                        const plasma::Matrix<float> &projection, const plasma::Vector<4, float> &tint)
{
    Sprite *sprite = m_staticEntitySprites[entity.type];
    // A chest (type 10): the body, then sprite 11 as the lid (below).
    if (entity.type == StaticObjectChest) {
        float scale = entity.size[0] / (float)sprite->sizeX();
        plasma::Matrix<float> model;
        model.identity();
        model.translate((float)(m_drawOriginX + entity.position[0]), (float)(m_drawOriginY + entity.position[1]),
                        (float)entity.position[2]);
        model.scale(scale, scale, scale);
        model.rotateZ((float)(entity.rotation * 90));
        model.translate((float)sprite->sizeX() * -0.5f, (float)sprite->sizeY() * -0.5f, 0.0f);
        // The light where the entity stands (the red channel).
        int light = m_world.cubeLight((int)entity.position[0], (int)entity.position[1], (int)entity.position[2], 0, 0);
        plasma::Vector<4, float> color(tint[0], tint[1], tint[2], light / 255.0f * tint[3]);
        m_shader->setMatrices(model, view, projection);
        m_shader->setMaterialColor(color);
        sprite->draw();
        sprite = m_staticEntitySprites[StaticObjectChestLid];
    }
    if (!sprite)
        return;
    float scale = entity.size[0] / (float)sprite->sizeX();
    plasma::Matrix<float> model;
    model.identity();
    // What rises and sinks when used: the gate (5) by 0.8 of its height, the spike trap (7) downwards
    // by its height and one block, the stomp trap (8) by its height.
    if (entity.type == StaticObjectGate)
        model.translate(0.0f, 0.0f, entity.size[2] * 0.8f * openness(entity, 0.001f));
    if (entity.type == StaticObjectSpikeTrap)
        model.translate(0.0f, 0.0f, -(entity.size[2] * openness(entity, 0.01f)) - 1.0f);
    if (entity.type == StaticObjectStompTrap)
        model.translate(0.0f, 0.0f, entity.size[2] * openness(entity, 0.01f));
    model.translate((float)(m_drawOriginX + entity.position[0]), (float)(m_drawOriginY + entity.position[1]),
                    (float)entity.position[2]);
    model.scale(scale, scale, scale);
    model.rotateZ((float)(entity.rotation * 90));
    model.translate((float)sprite->sizeX() * -0.5f, (float)sprite->sizeY() * -0.5f, 0.0f);
    // The doors (1..3) turn up to 90 degrees about the vertical edge at the middle of their depth.
    if (entity.type == StaticObjectDoor || entity.type == StaticObjectBigDoor || entity.type == StaticObjectWindow) {
        float open = openness(entity, 0.001f);
        model.translate(0.0f, (float)sprite->sizeY() * 0.5f, 0.0f);
        model.rotateZ(open * 90.0f);
        model.translate(0.0f, (float)sprite->sizeY() * -0.5f, 0.0f);
    }
    // The chest's lid tips up to 80 degrees about its back edge.
    if (entity.type == StaticObjectChest) {
        float open = openness(entity, 0.001f);
        model.translate(0.0f, 0.0f, 9.0f);
        model.rotateX(open * 80.0f);
        model.translate(0.0f, 0.0f, -1.0f);
    }
    // The lever (9) tips from -30 to 30 degrees about its middle.
    if (entity.type == StaticObjectLever) {
        float open = openness(entity, 0.001f);
        model.translate(0.0f, (float)sprite->sizeY() * 0.5f, -0.5f);
        model.rotateX(open * 60.0f - 30.0f);
        model.translate(0.0f, (float)sprite->sizeY() * -0.5f, 0.0f);
    }
    // The light where the entity stands (the red channel).
    int light = m_world.cubeLight((int)entity.position[0], (int)entity.position[1], (int)entity.position[2], 0, 0);
    plasma::Vector<4, float> color(tint[0], tint[1], tint[2], light / 255.0f * tint[3]);
    // The lamps (50, 51) are drawn at full light.
    if (entity.type == StaticObjectStreetLight || entity.type == StaticObjectFireStreetLight)
        color[3] = 1.0f;
    m_shader->setAlpha(distanceSquared > fadeStart * fadeStart
                           ? 1.0f - (sqrt(distanceSquared) - fadeStart) / (fadeEnd - fadeStart)
                           : 1.0f);
    m_shader->setMatrices(model, view, projection);
    m_shader->setMaterialColor(color);
    sprite->draw();
}

// The shape is drawRisingCubeRing's, the cubes arcing over (the pulse eight times slower).
void GameController::drawArcingCubeShell(const plasma::Vector<3, Fixed> &position,
                                         const plasma::Vector<4, float> &startColor,
                                         const plasma::Vector<4, float> &endColor, int time,
                                         const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection,
                                         float radius, float scale, int count)
{
    for (int i = 0; i < count; i++) {
        float progress = (float)((time + i * 443) % 2000) / 2000.0f;
        float arc = (float)(progress * 3.141592653589793);
        float height = cos(arc) * radius;
        float distance = sin(arc) * radius;
        float fade = (progress - 0.5f) * 2.0f;
        float pulse = cos((float)(i * 13 + (float)(time / 8 % 500) / 500.0f * 2.0f * 3.141592653589793));
        float size = (pulse * 0.5f + 1.0f) * scale * (1.0f - fade * fade);
        float angle = (float)(((float)i / 10.0f + (float)(time % 10000) * 0.0001f) * 2.0f * 3.141592653589793);
        m_shader->setMaterialColor(startColor * (1.0f - progress) + endColor * progress);

        plasma::Matrix<float> model;
        model.identity();
        model.translate(plasma::Vector<3, float>(cos(angle) * distance, sin(angle) * distance, height));
        model.translate((float)(m_drawOriginX + position[0]), (float)(m_drawOriginY + position[1]), (float)position[2]);
        model.rotateX(progress * 40.0f);
        model.rotateY(progress * 30.0f);
        model.rotateZ(progress * 10.0f);
        model.scale(size, size, size);
        model.translate((float)m_cubeSprite->sizeX() * -0.5f, (float)m_cubeSprite->sizeY() * -0.5f,
                        (float)m_cubeSprite->sizeZ() * -0.5f);
        m_shader->setMatrices(model, view, projection);
        m_cubeSprite->draw();
    }
}

bool GameController::isCameraAtFocus()
{
    return m_actualCameraDistance == 0.0f;
}

}
