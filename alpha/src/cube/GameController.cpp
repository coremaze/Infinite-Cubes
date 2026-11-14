#include "cube/WorldDimensions.h"
// GameController manages the game client, rendering chunks and saved-world previews.
#include "cube/GameController.h"
#include "cube/WorldBlocks.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include "cube/widgets/ChatWidget.h"
#include "cube/widgets/CharacterStyleWidget.h"
#include "cube/widgets/CharacterWidget.h"
#include "cube/Audio.h"
#include "cube/widgets/AdaptionWidget.h"
#include "cube/widgets/EnchantWidget.h"
#include "cube/widgets/BlueprintPreviewWidget.h"
#include "cube/widgets/CharacterPreviewWidget.h"
#include "cube/BytesIO.h"
#include "cube/Serializer.h"
#include "cube/widgets/VoxelWidget.h"
#include "cube/Chunk.h"
#include "cube/Vertex.h"
#include "cube/Field.h"
#include "cube/Region.h"
#include "cube/Noise.h"
#include "cube/CubeShader.h"
#include "cube/widgets/InventoryWidget.h"
#include "cube/widgets/SkillWidget.h"
#include "cube/SkillIds.h"
#include "cube/Creature.h"
#include "cube/Network.h"
#include "cube/widgets/MapOverlayWidget.h"
#include "cube/widgets/ObjectiveWidget.h"
#include "cube/widgets/OptionsWidget.h"
#include "cube/widgets/PreviewWidget.h"
#include "cube/widgets/SpeechWidget.h"
#include "cube/Sprite.h"
#include "cube/Server.h"
#include "cube/SoundIds.h"
#include "cube/SpriteIds.h"
#include "cube/RaceIds.h"
#include "cube/CreatureIds.h"
#include "cube/widgets/CharacterWidget.h"
#include "cube/widgets/SpriteWidget.h"
#include "cube/StatCurves.h"
#include "cube/widgets/StartMenuWidget.h"
#include "cube/widgets/StatisticsWidget.h"
#include "cube/Scripting.h"
#include "cube/System.h"
#include "cube/widgets/SystemWidget.h"
#include "cube/WorldInfo.h"
#include "cube/widgets/WorldPreviewWidget.h"
#include "plasma/Display.h"
#include "plasma/Engine.h"
#include "plasma/FontEngine.h"
#include "plasma/Font.h"
#include "plasma/Node.h"
#include "plasma/PLXStreamReader.h"
#include "plasma/ScalableFont.h"
#include "plasma/SmoothMeshShape.h"
#include "plasma/TextShape.h"
#include "plasma/Texture.h"
#include "plasma/Transformation.h"
#include "plasma/Widget.h"
#include "Thread.h"
#include <mmsystem.h>

namespace cube {
// GameController's tuning values (anonymous: this file's own). Distances are in blocks, angles in
// degrees, times in milliseconds, rates per millisecond (for blend and blendFactor, cube/Noise.h).
namespace {
// The camera.
enum {
    InitialCameraDistance = 16
}; // until the constructor zeroes it for the title screen
enum {
    TitleCameraPitch = 120
}; // the title screen's camera tilt
enum {
    CreationCameraDistance = 5
}; // the character and world creation screens
enum {
    CreationCameraPitch = 90
};
enum {
    WorldCreationCameraYaw = 180
}; // the world screens look at the scene from the other side
enum {
    MenuCameraDistance = 10
}; // the title, connect and list screens, the camera circling
enum {
    MenuCameraPitch = 100
};
enum {
    MaximumCameraDistance = 14
};
enum {
    CameraDistancePerWheelStep = 2
};
enum {
    CameraEscapeRayLength = 100
}; // a camera inside a block looks for open air from the player this far The map.
enum {
    MaximumMapZoom = 10
};
enum {
    MapDragSpeed = 4
}; // divided by the zoom The pointer and the lists of the start screens.
enum {
    ListDragSpeedGain = 2
}; // pointer pixels times milliseconds to scroll speed The streaming threads.
enum {
    ChunkVertexBudget = 0x1800000
}; // vertices meshed at a render distance of 100%
enum {
    ChunkThreadSleep = 5
};
enum {
    MapThreadSleep = 10
};
enum {
    ZoneThreadSleep = 20
};
enum {
    PlayableMeshRadius = 8
}; // "Please wait..." until the meshes reach this far around the camera The world and the player.
enum {
    ChunkGridSide = 40
}; // chunks per side of the view square
enum {
    InitialSeed = 123
}; // replaced by timeGetTime before the world is set up
enum {
    PlantSpriteSlots = 64
}; // m_plantSprites, by plant type
enum {
    PartyLifeBarCount = 3
}; // the other members of a party of four
enum {
    PetSearchRadiusSquared = 25
}; // the pet nearest the player, within five blocks
enum {
    RemoteSmoothingTime = 60
}; // a moved creature is drawn gliding to its new state
enum {
    ClassMasterRangeSquared = 16
}; // four blocks
enum {
    PetNameLengthLimit = 16
}; // characters; longer names are refused
enum {
    ExtraMaterialChance = 5
}; // one in five crafts of a material yields one more, up to three more
enum {
    FormulaModifierCount = 11
}; // a learnt armour formula's random modifier, 0..10
enum {
    WarriorWeaponKinds = 3
}; // sword, axe, mace (one-handed or two-handed)
enum {
    SlowMotionSleep = 5
};
enum {
    GliderAcceleration = 10
}; // along the course
enum {
    AimHoldTime = 200
}; // after an attack the player keeps facing the aim this long
enum {
    RetreatSpeed = 20
}; // horizontal, plus RetreatSkillSpeed times the skill's bonus
enum {
    RetreatSkillSpeed = 15
};
enum {
    RetreatRise = 5
}; // vertical, plus RetreatSkillRise times the skill's bonus
enum {
    RetreatSkillRise = 12
};
// Particles, lights and pointing.
enum {
    VoxelParticleDelay = 1000
}; // a broken model's particles start up to this late
enum {
    VoxelParticleLifetime = 5000
};
enum {
    ImpactParticleChance = 8
}; // one in eight steps of a clipped impact sheds a particle
enum {
    ParticleGravity = 30
}; // blocks per second squared
enum {
    HitLightDuration = 400
};
enum {
    WeaponHitLightRadius = 20
};
enum {
    SupportRayLightRadius = 16
};
enum {
    CreaturePickingRange = 256
}; // in full light; the camera points at creatures within it
enum {
    CreatureLabelRange = 200
}; // in full light; the life bars show within it
enum {
    ObjectPickingRange = 200
};
enum {
    ItemPickingRange = 200
};
enum {
    ItemPickupRangeSquared = 100
}; // ten blocks The skill points: one per four mana cubes and two per level after the first.
enum {
    ManaCubesPerSkillPoint = 4
};
// The ability bar's class abilities (from AbilityBarKey1) are the class skills.
enum {
    BarSkillOffset = SkillClassAbilityFirst - AbilityBarKey1
};
}

// Reads a list the server wrote with its writeList: the count, then each element's bytes.
template<class T> void readList(BytesIO &data, std::list<T> &list)
{
    int count; // unset when the data ends early
    data.read(&count, sizeof(count));
    for (int i = 0; i < count; i++) {
        T element;
        data.read(&element, sizeof(T));
        list.push_back(element);
    }
}

// A nearby chunk records the chunk pointer, desired coordinates and squared distance from the grid center in chunk units.
struct NearbyChunk {
    Chunk *chunk;
    int distance;
    plasma::Vector<2, int> zone;
};

Chunk::Chunk()
    : position(-1, -1),
      remeshTime(0),
      needsRemesh(false),
      vertexCount(0)
{
    InitializeCriticalSectionAndSpinCount(&lock, 0x80000400);
}

static plasma::SmoothMeshShape *smoothMeshShape(plasma::Shape *shape);
static plasma::TextShape *textShape(plasma::Shape *shape);

GameController::GameController(IDirect3DDevice9 *device, CubeShader *shader, plasma::Engine *engine, Audio *audio,
                               Options *options, std::vector<plasma::Vector<2, int>> *resolutions, bool server,
                               const std::string &serverAddress)
    : Controller(device),
      m_cameraPosition(10000, 10000, 60),
      m_cameraFocus(0, 0, 0),
      m_options(*options),
      m_quit(false),
      m_cameraRotation(0.0f, 0.0f, 0.0f),
      m_targetCameraRotation(0.0f, 0.0f, 0.0f),
      m_targetCameraDistance(InitialCameraDistance),
      m_cameraDistance(InitialCameraDistance),
      m_mapZoom(1.0f),
      m_targetMapZoom(1.0f),
      m_loadedMeshRadius(0),
      m_screenShake(0),
      m_viewDistance(ChunkGridSide),
      m_chunks(0),
      m_world(device, false),
      m_shader(shader),
      m_connected(false),
      m_receiveThread(0),
      m_sendThread(0),
      m_serverAddress(serverAddress),
      m_localServer(0),
      m_socket(0),
      m_target(0),
      m_shadowTexture(0),
      m_sunTexture(0),
      m_mapOpen(false),
      m_slowMotion(false),
      m_drawPlayerOnly(false),
      m_frameMilliseconds(0),
      m_buildingMode(false),
      m_engine(engine),
      m_audio(audio),
      m_showAllLifeBars(false),
      unused800870(0),
      m_tradeCreature(0),
      m_equipmentOpen(false),
      m_skillsOpen(false),
      m_gameMenuOpen(false),
      m_conversationWidget(0),
      m_showMultiplayerWorlds(false),
      m_characterListScrollSpeed(0),
      m_worldListScrollSpeed(0),
      m_selectedCharacter(0),
      m_selectedWorld(0),
      m_chatWidget(0),
      m_previewRotation(0.0f, 0.0f, 0.0f),
      m_ringMenuOpen(false),
      m_ringMenuSelection(0),
      m_ringRadius(0),
      m_ringRotation(0),
      m_seed(InitialSeed),
      m_interactionCreatureId(0),
      m_hoveredGroundItem(-1, -1, 0),
      m_hoveredObject(-1, -1, 0),
      m_worldMap(device, &m_world, m_shader),
      m_mapOffset(0.0f, 0.0f, 0.0f),
      m_spiritTimer(0),
      m_craftingProgress(-1.0f),
      m_showPathSearch(false)
{
    m_world.m_spriteManager.loadSprites("");
    std::string unused = "";
#if CUBE_SCRIPTING
    System system(false);
#endif
    m_seed = timeGetTime();
    m_world.setup(m_seed, m_worldName);

    // The screens, children of the engine's root; only the game's is shown.
    m_gameScreen = m_engine->createNode(0, 0, 0, m_engine->root());
    m_characterCreationScreen = m_engine->createNode(0, 0, 0, m_engine->root());
    m_characterCreationScreen->setVisible(false);
    m_worldCreationScreen = m_engine->createNode(0, 0, 0, m_engine->root());
    m_worldCreationScreen->setVisible(false);
    m_characterListScreen = m_engine->createNode(0, 0, 0, m_engine->root());
    m_characterListScreen->setVisible(false);
    m_connectScreen = m_engine->createNode(0, 0, 0, m_engine->root());
    m_connectScreen->setVisible(false);
    m_worldListScreen = m_engine->createNode(0, 0, 0, m_engine->root());
    m_worldListScreen->setVisible(false);
    m_startScreen = m_engine->createNode(0, 0, 0, m_engine->root());

    m_targetCameraDistance = 0.0f;
    m_cameraDistance = 0.0f;
    m_cameraRotation[0] = TitleCameraPitch;
    m_cameraRotation[2] = 0.0f;
    m_targetCameraRotation = m_cameraRotation;
    m_viewRadius = m_viewDistance * 0.5f * BlocksPerChunk;

    m_textFxNode = m_engine->createNode(0, 0, 0, m_engine->root(), L"textFX");
    m_minimapNode = m_engine->createNode(0, 0, 0, m_gameScreen, L"minimap");

    // The database file is checked by the interface script (in builds that have it: cube/Scripting.h).
    std::ifstream file("db.dat");
    file.seekg(0, std::ios::end);
    int size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size == 0)
        return;
    std::string databaseScriptSource(size, ' ');
    file.read(&databaseScriptSource[0], size);
    file.close();
#if CUBE_SCRIPTING
    m_quit = system.m_machine.evaluate<int>("db", system.m_firstText, databaseScriptSource) == 0;
#endif

    // The interface is laid out in pixels: below, the text shapes' sizes, stroke radii and line
    // spacings, the widgets' sizes (resizeTo: width, height) and places (moveTo, translations: x, y),
    // and the colours (RGBA, 0..1), are  hand-set values. The name of the landscape the
    // player is in, with its shadow.
    plasma::TextShape *shadow = m_engine->createTextShape(L"Hyara Planes");
    shadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    shadow->m_pixelSize = 20.0f;
    shadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    shadow->m_strokeRadius = 3.0f;
    shadow->m_lineSpacing = 3.0f;
    shadow->m_fontName = L"resource1.dat";
    shadow->setFlags(1 << plasma::TEXT_CENTER);
    shadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_landscapeNode = m_engine->createNode(0, shadow, 0, m_gameScreen, L"landscape");
    m_landscapeNode->transformation()->m_translation =
        plasma::Vector<2, float>(20.0f, shadow->m_pixelSize + 5.0f + shadow->m_pixelSize);
    m_landscapeNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_landscapeText = m_engine->createTextShape(L"Hyara Planes");
    m_landscapeText->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_landscapeText->m_pixelSize = shadow->m_pixelSize;
    m_landscapeText->m_lineSpacing = shadow->m_lineSpacing;
    m_landscapeText->m_fontName = L"resource1.dat";
    m_landscapeText->setFlags(1 << plasma::TEXT_CENTER);
    m_landscapeText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, m_landscapeText, 0, m_landscapeNode, L"landscape");

    plasma::TextShape *detailShadow = m_engine->createTextShape(L"Hyara Planes");
    detailShadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    detailShadow->m_pixelSize = 12.0f;
    detailShadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    detailShadow->m_strokeRadius = 3.0f;
    detailShadow->m_lineSpacing = 3.0f;
    detailShadow->m_fontName = L"resource1.dat";
    detailShadow->setFlags(1 << plasma::TEXT_CENTER);
    detailShadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    plasma::Node *detail = m_engine->createNode(0, detailShadow, 0, m_landscapeNode, L"landscapedetail");
    detail->transformation()->m_translation = plasma::Vector<2, float>(0.0f, 20.0f);
    detail->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    plasma::TextShape *detailText = m_engine->createTextShape(L"Hyara Planes");
    detailText->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    detailText->m_pixelSize = detailShadow->m_pixelSize;
    detailText->m_lineSpacing = detailShadow->m_lineSpacing;
    detailText->m_fontName = L"resource1.dat";
    detailText->setFlags(1 << plasma::TEXT_CENTER);
    detailText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, detailText, 0, detail, L"landscapedetail");

    // "Please wait...", on the root.
    plasma::TextShape *waitShadow = m_engine->createTextShape(L"");
    waitShadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    waitShadow->m_pixelSize = 12.0f;
    waitShadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    waitShadow->m_strokeRadius = 3.0f;
    waitShadow->m_lineSpacing = 3.0f;
    waitShadow->m_string = L"Please wait...";
    waitShadow->m_fontName = L"resource1.dat";
    waitShadow->setFlags(1 << plasma::TEXT_CENTER);
    waitShadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_waitNode = m_engine->createNode(0, waitShadow, 0, m_engine->root(), L"wait");
    plasma::TextShape *waitText = m_engine->createTextShape(L"");
    waitText->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    waitText->m_pixelSize = waitShadow->m_pixelSize;
    waitText->m_lineSpacing = waitShadow->m_lineSpacing;
    waitText->m_string = waitShadow->m_string;
    waitText->m_fontName = L"resource1.dat";
    waitText->setFlags(1 << plasma::TEXT_CENTER);
    waitText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, waitText, 0, m_waitNode, L"wait");

    // The name error of the character creation, in red.
    plasma::TextShape *nameErrorShadow = m_engine->createTextShape(L"");
    nameErrorShadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    nameErrorShadow->m_pixelSize = 12.0f;
    nameErrorShadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    nameErrorShadow->m_strokeRadius = 3.0f;
    nameErrorShadow->m_lineSpacing = 3.0f;
    nameErrorShadow->m_string = L"Error";
    nameErrorShadow->m_fontName = L"resource1.dat";
    nameErrorShadow->setFlags(1 << plasma::TEXT_CENTER);
    nameErrorShadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_nameError = m_engine->createNode(0, nameErrorShadow, 0, m_engine->root(), L"nameError");
    plasma::TextShape *nameErrorText = m_engine->createTextShape(L"");
    nameErrorText->m_color = plasma::Vector<4, float>(1.0f, 0.25f, 0.25f, 1.0f);
    nameErrorText->m_pixelSize = nameErrorShadow->m_pixelSize;
    nameErrorText->m_lineSpacing = nameErrorShadow->m_lineSpacing;
    nameErrorText->m_string = nameErrorShadow->m_string;
    nameErrorText->m_fontName = L"resource1.dat";
    nameErrorText->setFlags(1 << plasma::TEXT_CENTER);
    nameErrorText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, nameErrorText, 0, m_nameError, L"nameError");

    // The connection error, on the connect screen.
    plasma::TextShape *connectionErrorShadow = m_engine->createTextShape(L"");
    connectionErrorShadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    connectionErrorShadow->m_pixelSize = 12.0f;
    connectionErrorShadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    connectionErrorShadow->m_strokeRadius = 3.0f;
    connectionErrorShadow->m_lineSpacing = 3.0f;
    connectionErrorShadow->m_string = L"";
    connectionErrorShadow->m_fontName = L"resource1.dat";
    connectionErrorShadow->setFlags(1 << plasma::TEXT_CENTER);
    connectionErrorShadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_connectionErrorNode = m_engine->createNode(0, connectionErrorShadow, 0, m_connectScreen, L"connectionError");
    plasma::TextShape *connectionErrorText = m_engine->createTextShape(L"");
    connectionErrorText->m_color = plasma::Vector<4, float>(1.0f, 0.25f, 0.25f, 1.0f);
    connectionErrorText->m_pixelSize = connectionErrorShadow->m_pixelSize;
    connectionErrorText->m_lineSpacing = connectionErrorShadow->m_lineSpacing;
    connectionErrorText->m_string = connectionErrorShadow->m_string;
    connectionErrorText->m_fontName = L"resource1.dat";
    connectionErrorText->setFlags(1 << plasma::TEXT_CENTER);
    connectionErrorText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, connectionErrorText, 0, m_connectionErrorNode, L"connectionError");

    // The information text of the game screen.
    plasma::TextShape *infoShadow = m_engine->createTextShape(L"");
    infoShadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    infoShadow->m_pixelSize = 12.0f;
    infoShadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    infoShadow->m_strokeRadius = 2.0f;
    infoShadow->m_fontName = L"resource1.dat";
    infoShadow->setFlags(1 << plasma::TEXT_RIGHT);
    infoShadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_infoNode = m_engine->createNode(0, infoShadow, 0, m_gameScreen, L"info");
    m_infoNode->transformation()->m_translation =
        plasma::Vector<2, float>(20.0f, infoShadow->m_pixelSize + 5.0f + infoShadow->m_pixelSize);
    m_infoNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_infoText = m_engine->createTextShape(L"");
    m_infoText->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_infoText->m_pixelSize = infoShadow->m_pixelSize;
    m_infoText->m_fontName = L"resource1.dat";
    m_infoText->setFlags(1 << plasma::TEXT_RIGHT);
    m_infoText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, m_infoText, 0, m_infoNode, L"info");

    unsigned __int64 key = plasma::keyFromPhrase("PlasmaXGraphics");
    std::vector<unsigned __int64> keys(1, key);

    m_mapScreen = m_engine->createNode(0, 0, 0, m_engine->root());
    m_mapOverlayNode = m_engine->createNode(0, 0, 0, m_mapScreen);
    new MapOverlayWidget(m_engine, m_mapOverlayNode, this);

    // The interface's graphics; the templates are taken out of them.
    m_engine->readPlasmaFileIntoNode(L"gui.plx", m_gameScreen, (1 << plasma::LOAD_HINTED_TEXT), 0, keys);
    m_engine->readPlasmaFileIntoNode(L"quest-tag.plx", m_gameScreen, (1 << plasma::LOAD_HINTED_TEXT), 0, keys);
    m_helpNode = m_engine->createNode(0, 0, 0, m_gameScreen);
    m_engine->readPlasmaFileIntoNode(L"help.plx", m_helpNode, (1 << plasma::LOAD_HINTED_TEXT), 0, keys);
    m_helpNode->setHitTestable(false);
    m_helpNode->setVisible(false);
    m_questTag = m_gameScreen->findNode(L"questtag");
    m_smallQuestTag = m_gameScreen->findNode(L"smallquesttag");
    m_engine->readPlasmaFileIntoNode(L"start.plx", m_startScreen, (1 << plasma::LOAD_HINTED_TEXT), 0, keys);
    m_cubeWorldLogo = m_startScreen->findNode(L"cubeworld");
    m_picromaLogo = m_startScreen->findNode(L"picroma");

    // The landscape name's stars.
    plasma::Node *star = m_gameScreen->findNode(L"star1")->clone(m_landscapeNode);
    star->transformation()->m_translation = plasma::Vector<2, float>(-20.0f, -65.0f);
    star->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    star = m_gameScreen->findNode(L"star2")->clone(m_landscapeNode);
    star->transformation()->m_translation = plasma::Vector<2, float>(-20.0f, -65.0f);
    star->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    star = m_gameScreen->findNode(L"star3")->clone(m_landscapeNode);
    star->transformation()->m_translation = plasma::Vector<2, float>(-20.0f, -65.0f);
    star->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    star = m_gameScreen->findNode(L"star4")->clone(m_landscapeNode);
    star->transformation()->m_translation = plasma::Vector<2, float>(-20.0f, -65.0f);
    star->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);

    m_landNameNode = m_gameScreen->findNode(L"landname");
    m_blackWidget = m_gameScreen->findNode(L"blackwidget");
    m_blackWidget->setParent(0);
    m_itemBox = m_gameScreen->findNode(L"itembox");
    m_itemBox->setParent(0);
    m_wideItemBox = m_gameScreen->findNode(L"wideitembox");
    m_wideItemBox->setParent(0);
    m_itemSelector = m_gameScreen->findNode(L"itemselector");
    m_itemSelector->setParent(0);
    m_comboPoints = m_gameScreen->findNode(L"combopoints");
    plasma::Node *leftButton = m_gameScreen->findNode(L"leftbutton");
    leftButton->setParent(0);
    plasma::Node *rightButton = m_gameScreen->findNode(L"rightbutton");
    rightButton->setParent(0);
    plasma::Node *upButton = m_gameScreen->findNode(L"upbutton");
    upButton->setParent(0);
    plasma::Node *downButton = m_gameScreen->findNode(L"downbutton");
    downButton->setParent(0);
    plasma::Node *scrollButton = m_gameScreen->findNode(L"scrollbutton");
    scrollButton->setParent(0);
    plasma::Node *edit = m_gameScreen->findNode(L"edit");
    edit->setParent(0);
    // The button templates' children must not take the pointer.
    plasma::Node *button = m_gameScreen->findNode(L"button");
    for (std::list<plasma::Node *>::iterator it = button->m_children.begin(); it != button->m_children.end(); it++)
        (*it)->setHitTestable(false);
    button->setParent(0);
    plasma::Node *secondaryButtonTemplate = m_gameScreen->findNode(L"button2");
    for (std::list<plasma::Node *>::iterator it = secondaryButtonTemplate->m_children.begin();
         it != secondaryButtonTemplate->m_children.end(); it++)
        (*it)->setHitTestable(false);
    secondaryButtonTemplate->setParent(0);
    plasma::Node *smallButton = m_gameScreen->findNode(L"smallbutton");
    for (std::list<plasma::Node *>::iterator it = smallButton->m_children.begin(); it != smallButton->m_children.end();
         it++)
        (*it)->setHitTestable(false);
    smallButton->setParent(0);
    m_previewNode = m_gameScreen->findNode(L"preview");
    m_equippedPreviewNode = m_gameScreen->findNode(L"equippedpreview");

    // The start menu's buttons.
    m_createCharacterButton = button->clone(m_characterCreationScreen);
    m_createCharacterButton->widget()->resizeTo(300.0f, 40.0f, true);
    m_createCharacterButton->setText(L"Create Character");
    m_createCharacterButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this,
                                               &GameController::onDoneCharacter, true, true);
    m_createWorldButton = button->clone(m_worldCreationScreen);
    m_createWorldButton->widget()->resizeTo(300.0f, 40.0f, true);
    m_createWorldButton->setText(L"Create World");
    m_createWorldButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this, &GameController::onDoneWorld,
                                           true, true);
    m_backButton = button->clone(engine->root());
    m_backButton->widget()->resizeTo(200.0f, 40.0f, true);
    m_backButton->setText(L"Back");
    m_backButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this, &GameController::onBack, true, true);
    m_worldListButton = button->clone(engine->root());
    m_worldListButton->widget()->resizeTo(300.0f, 40.0f, true);
    m_worldListButton->widget()->moveTo(40.0f, 40.0f, true);
    m_worldListButton->setText(L"Multiplayer Worlds...");
    m_worldListButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this, &GameController::onToggleWorlds,
                                         true, true);
    m_connectScreenButton = button->clone(engine->root());
    m_connectScreenButton->widget()->resizeTo(300.0f, 40.0f, true);
    m_connectScreenButton->widget()->moveTo(40.0f, 40.0f, true);
    m_connectScreenButton->setText(L"Connect to server");
    m_connectScreenButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this,
                                             &GameController::showConnectScreen, true, true);
    m_connectButton = button->clone(engine->root());
    m_connectButton->widget()->resizeTo(200.0f, 40.0f, true);
    m_connectButton->widget()->moveTo(40.0f, 40.0f, true);
    m_connectButton->setText(L"Connect");
    m_connectButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this, &GameController::connectToServer,
                                       true, true);

    // The character creation's name, the world creation's name and seed and the connect screen's
    // address: each a label with an edit field.
    plasma::TextShape *characterNameShadow = m_engine->createTextShape(std::wstring());
    characterNameShadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    characterNameShadow->m_pixelSize = 12.0f;
    characterNameShadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    characterNameShadow->m_strokeRadius = 3.0f;
    characterNameShadow->m_lineSpacing = 3.0f;
    characterNameShadow->m_string = L"Name";
    characterNameShadow->setFlags(1 << plasma::TEXT_CENTER);
    characterNameShadow->m_fontName = L"resource1.dat";
    characterNameShadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_characterNameNode = m_engine->createNode(0, characterNameShadow, 0, m_characterCreationScreen, L"charactername");
    plasma::TextShape *characterNameText = m_engine->createTextShape(std::wstring());
    characterNameText->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    characterNameText->m_pixelSize = characterNameShadow->m_pixelSize;
    characterNameText->m_lineSpacing = characterNameShadow->m_lineSpacing;
    characterNameText->m_string = characterNameShadow->m_string;
    characterNameText->setFlags(1 << plasma::TEXT_CENTER);
    characterNameText->m_fontName = L"resource1.dat";
    characterNameText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, characterNameText, 0, m_characterNameNode, L"charactername");
    plasma::Node *nameEdit = edit->clone(m_characterNameNode);
    float x = m_characterNameNode->transformation()->m_translation.value()[0];
    nameEdit->widget()->moveTo(x - nameEdit->widget()->viewWidth() * 0.5f,
                               m_characterNameNode->transformation()->m_translation.value()[1] + 10.0f, true);
    m_characterNameNode->setText(L"edit", L"", true);

    plasma::TextShape *worldNameShadow = m_engine->createTextShape(std::wstring());
    worldNameShadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    worldNameShadow->m_pixelSize = 12.0f;
    worldNameShadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    worldNameShadow->m_strokeRadius = 3.0f;
    worldNameShadow->m_lineSpacing = 3.0f;
    worldNameShadow->m_string = L"Name";
    worldNameShadow->setFlags(1 << plasma::TEXT_CENTER);
    worldNameShadow->m_fontName = L"resource1.dat";
    worldNameShadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_worldNameNode = m_engine->createNode(0, worldNameShadow, 0, m_worldCreationScreen, L"worldName");
    plasma::TextShape *worldNameText = m_engine->createTextShape(std::wstring());
    worldNameText->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    worldNameText->m_pixelSize = worldNameShadow->m_pixelSize;
    worldNameText->m_lineSpacing = worldNameShadow->m_lineSpacing;
    worldNameText->m_string = worldNameShadow->m_string;
    worldNameText->setFlags(1 << plasma::TEXT_CENTER);
    worldNameText->m_fontName = L"resource1.dat";
    worldNameText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, worldNameText, 0, m_worldNameNode, L"worldName");
    nameEdit = edit->clone(m_worldNameNode);
    x = m_worldNameNode->transformation()->m_translation.value()[0];
    nameEdit->widget()->moveTo(x - nameEdit->widget()->viewWidth() * 0.5f,
                               m_worldNameNode->transformation()->m_translation.value()[1] + 10.0f, true);
    m_worldNameNode->setText(L"edit", L"", true);

    plasma::TextShape *worldSeedShadow = m_engine->createTextShape(std::wstring());
    worldSeedShadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    worldSeedShadow->m_pixelSize = 12.0f;
    worldSeedShadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    worldSeedShadow->m_strokeRadius = 3.0f;
    worldSeedShadow->m_lineSpacing = 3.0f;
    worldSeedShadow->m_string = L"Seed (must be a number)";
    worldSeedShadow->setFlags(1 << plasma::TEXT_CENTER);
    worldSeedShadow->m_fontName = L"resource1.dat";
    worldSeedShadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_worldSeedNode = m_engine->createNode(0, worldSeedShadow, 0, m_worldCreationScreen, L"worldSeed");
    plasma::TextShape *worldSeedText = m_engine->createTextShape(std::wstring());
    worldSeedText->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    worldSeedText->m_pixelSize = worldSeedShadow->m_pixelSize;
    worldSeedText->m_lineSpacing = worldSeedShadow->m_lineSpacing;
    worldSeedText->m_string = worldSeedShadow->m_string;
    worldSeedText->setFlags(1 << plasma::TEXT_CENTER);
    worldSeedText->m_fontName = L"resource1.dat";
    worldSeedText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, worldSeedText, 0, m_worldSeedNode, L"worldSeed");
    nameEdit = edit->clone(m_worldSeedNode);
    x = m_worldSeedNode->transformation()->m_translation.value()[0];
    nameEdit->widget()->moveTo(x - nameEdit->widget()->viewWidth() * 0.5f,
                               m_worldSeedNode->transformation()->m_translation.value()[1] + 10.0f, true);
    m_worldSeedNode->setText(L"edit", L"", true);

    plasma::TextShape *serverNameShadow = m_engine->createTextShape(std::wstring());
    serverNameShadow->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    serverNameShadow->m_pixelSize = 12.0f;
    serverNameShadow->m_strokeColor = plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f);
    serverNameShadow->m_strokeRadius = 3.0f;
    serverNameShadow->m_lineSpacing = 3.0f;
    serverNameShadow->m_string = L"Server address";
    serverNameShadow->setFlags(1 << plasma::TEXT_CENTER);
    serverNameShadow->m_fontName = L"resource1.dat";
    serverNameShadow->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_serverAddressNode = m_engine->createNode(0, serverNameShadow, 0, m_connectScreen, L"serverName");
    plasma::TextShape *serverNameText = m_engine->createTextShape(std::wstring());
    serverNameText->m_color = plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    serverNameText->m_pixelSize = serverNameShadow->m_pixelSize;
    serverNameText->m_lineSpacing = serverNameShadow->m_lineSpacing;
    serverNameText->m_string = serverNameShadow->m_string;
    serverNameText->setFlags(1 << plasma::TEXT_CENTER);
    serverNameText->m_fontName = L"resource1.dat";
    serverNameText->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->createNode(0, serverNameText, 0, m_serverAddressNode, L"serverName");
    nameEdit = edit->clone(m_serverAddressNode);
    x = m_serverAddressNode->transformation()->m_translation.value()[0];
    nameEdit->widget()->moveTo(x - nameEdit->widget()->viewWidth() * 0.5f,
                               m_serverAddressNode->transformation()->m_translation.value()[1] + 10.0f, true);
    m_serverAddressNode->setText(L"edit", L"", true);

    // The game quits when the fonts are missing.
    if (m_engine->getFontEngine()->getFont(L"resource1.dat") == 0)
        m_quit = true;
    if (m_engine->getFontEngine()->getFont(L"resource2.dat") == 0)
        m_quit = true;

    m_crosshair = m_gameScreen->findNode(L"crosshair");
    m_zoomCrosshair = m_gameScreen->findNode(L"zoomcrosshair");
    m_lockedEnemy = m_gameScreen->findNode(L"lockedenemy");
    m_lockedFriend = m_gameScreen->findNode(L"lockedfriend");
    m_lockedEnemy->setVisible(false);
    m_lockedFriend->setVisible(false);
    plasma::Node *abilityButton = m_gameScreen->findNode(L"abilitybutton");
    plasma::Node *skillButton = m_gameScreen->findNode(L"skillbutton");
    plasma::Node *specializationButton = m_gameScreen->findNode(L"specializationbutton");
    plasma::Node *childSkillButton = m_gameScreen->findNode(L"childskillbutton");
    plasma::Node *quickItemButton = m_gameScreen->findNode(L"quickitembutton");
    plasma::Node *menuButton = m_gameScreen->findNode(L"menubutton");

    // The icons, from data3.db (or from the files when the database does not have them).
    std::string directory;
    Database *database = new Database();
    database->open("data3.db");
    m_abilityIcons[AbilityNone] = loadTexture(directory + "none.png", database);
    m_abilityIcons[AbilityTwoHandedSweep] = loadTexture(directory + "slam.png", database);
    m_abilityIcons[AbilityTwoHandedThrust] = loadTexture(directory + "slam-back.png", database);
    m_abilityIcons[AbilityTwoHandedSpin] = loadTexture(directory + "slam-left.png", database);
    m_abilityIcons[AbilityTwoHandedSlam] = loadTexture(directory + "slam-right.png", database);
    m_abilityIcons[AbilityLeftSlash] = loadTexture(directory + "attack-left.png", database);
    m_abilityIcons[AbilityRightSlash] = loadTexture(directory + "attack-right.png", database);
    m_abilityIcons[Ability4] = loadTexture(directory + "stab-left.png", database);
    m_abilityIcons[Ability3] = loadTexture(directory + "stab-right.png", database);
    m_abilityIcons[AbilityTwoHandedSwing] = loadTexture(directory + "jab.png", database);
    m_abilityIcons[AbilityCrush] = loadTexture(directory + "charge-slam.png", database);
    m_abilityIcons[AbilitySwirl] = loadTexture(directory + "swirl.png", database);
    m_abilityIcons[AbilityMonsterCharge] = loadTexture(directory + "charge-mutilate.png", database);
    m_abilityIcons[AbilityBlock] = loadTexture(directory + "block.png", database);
    m_abilityIcons[AbilityShieldAttack] = loadTexture(directory + "shield-attack.png", database);
    m_abilityIcons[AbilityBlockJab] = loadTexture(directory + "shield-slam.png", database);
    m_abilityIcons[AbilityLeftDaggerPierce] = loadTexture(directory + "pierce-left.png", database);
    m_abilityIcons[AbilityRightDaggerPierce] = loadTexture(directory + "pierce-right.png", database);
    m_abilityIcons[AbilityAmbush] = loadTexture(directory + "ambush.png", database);
    m_abilityIcons[AbilityLeftStab] = loadTexture(directory + "punch-left.png", database);
    m_abilityIcons[AbilityRightStab] = loadTexture(directory + "punch-right.png", database);
    m_abilityIcons[AbilityKick] = loadTexture(directory + "kick.png", database);
    m_abilityIcons[AbilityRangerKick] = loadTexture(directory + "ranger-kick.png", database);
    m_abilityIcons[AbilityLeftSweep] = loadTexture(directory + "slash-left.png", database);
    m_abilityIcons[AbilityRightSweep] = loadTexture(directory + "slash-right.png", database);
    m_abilityIcons[AbilityPerforate] = loadTexture(directory + "perforate.png", database);
    m_abilityIcons[AbilitySneak] = loadTexture(directory + "sneak.png", database);
    m_abilityIcons[AbilityAim] = loadTexture(directory + "aim.png", database);
    m_abilityIcons[AbilitySwiftness] = loadTexture(directory + "swiftness.png", database);
    m_abilityIcons[AbilityWandFirebolt] = loadTexture(directory + "firebolt.png", database);
    m_abilityIcons[AbilityFirebolt] = loadTexture(directory + "firebolt.png", database);
    m_abilityIcons[AbilityBraceletFirebolt] = loadTexture(directory + "firebolt.png", database);
    m_abilityIcons[AbilityFireball] = loadTexture(directory + "fireball.png", database);
    m_abilityIcons[AbilityFireSalvo] = loadTexture(directory + "fire-salvo.png", database);
    m_abilityIcons[AbilityFireExplosion] = loadTexture(directory + "fire-explosion.png", database);
    m_abilityIcons[AbilityWandWaterDrop] = loadTexture(directory + "waterbolt.png", database);
    m_abilityIcons[AbilityWaterDrop] = loadTexture(directory + "waterbolt.png", database);
    m_abilityIcons[AbilityBraceletWaterDrop] = loadTexture(directory + "waterbolt.png", database);
    m_abilityIcons[AbilityWaterSplash] = loadTexture(directory + "waterball.png", database);
    m_abilityIcons[AbilityWaterSalvo] = loadTexture(directory + "water-salvo.png", database);
    m_abilityIcons[AbilityHealingStream] = loadTexture(directory + "healing-stream.png", database);
    m_abilityIcons[AbilityFireSwirl] = loadTexture(directory + "fire-shock.png", database);
    m_abilityIcons[AbilityFireVortex] = loadTexture(directory + "fire-burst.png", database);
    m_abilityIcons[AbilityWaterSwirl] = loadTexture(directory + "water-shock.png", database);
    m_abilityIcons[AbilityWaterVortex] = loadTexture(directory + "water-burst.png", database);
    m_abilityIcons[AbilitySmash] = loadTexture(directory + "smash.png", database);
    m_abilityIcons[AbilitySwirlStrike] = loadTexture(directory + "swirl.png", database);
    m_abilityIcons[AbilityCyclone] = loadTexture(directory + "cyclone.png", database);
    m_abilityIcons[AbilityIntercept] = loadTexture(directory + "intercept.png", database);
    m_abilityIcons[AbilityTeleport] = loadTexture(directory + "teleport.png", database);
    m_abilityIcons[AbilityRetreat] = loadTexture(directory + "retreat2.png", database);
    m_abilityIcons[AbilityCrossedWeaponSweep] = loadTexture(directory + "slash.png", database);
    m_abilityIcons[AbilityMonsterChargeStrike] = loadTexture(directory + "mutilate.png", database);
    m_abilityIcons[AbilityShoot] = loadTexture(directory + "shoot.png", database);
    m_abilityIcons[AbilityChargedShotRelease] = loadTexture(directory + "shoot-fast.png", database);
    m_abilityIcons[AbilitySalvoRelease] = loadTexture(directory + "salvo.png", database);
    m_abilityIcons[AbilityThrowBoomerang] = loadTexture(directory + "boomerang.png", database);
    m_abilityIcons[AbilityChargeBoomerang] = loadTexture(directory + "charge-boomerang.png", database);
    m_abilityIcons[AbilityChargedShot] = loadTexture(directory + "charge-shoot.png", database);
    m_abilityIcons[AbilitySalvo] = loadTexture(directory + "salvo.png", database);
    m_abilityIcons[AbilitySupportRay] = loadTexture(directory + "beam.png", database);
    m_abilityIcons[AbilityFireRay] = loadTexture(directory + "beam.png", database);
    m_abilityIcons[AbilityChargeFireball] = loadTexture(directory + "charge-bullet.png", database);
    m_abilityIcons[AbilityShuriken] = loadTexture(directory + "shuriken.png", database);
    m_abilityIcons[AbilityBulwark] = loadTexture(directory + "bulwark.png", database);
    m_abilityIcons[AbilityManaShield] = loadTexture(directory + "mana-shield.png", database);
    m_abilityIcons[AbilityCamouflage] = loadTexture(directory + "camouflage.png", database);
    m_abilityIcons[AbilityWarFrenzy] = loadTexture(directory + "warfrenzy.png", database);
    m_skillIcons[SkillPetRiding] = loadTexture(directory + "pet-riding.png", database);
    m_skillIcons[SkillPetTaming] = loadTexture(directory + "pet-taming.png", database);
    m_skillIcons[SkillHangGliding] = loadTexture(directory + "hang-gliding.png", database);
    m_skillIcons[SkillClimbing] = loadTexture(directory + "climbing.png", database);
    m_skillIcons[SkillSwimming] = loadTexture(directory + "swimming.png", database);
    m_skillIcons[SkillBoatDriving] = loadTexture(directory + "ship-driving.png", database);
    m_specializationIcons[std::make_pair(ClassMage, SpecializationFire)] =
        loadTexture(directory + "mage-fire.png", database);
    m_specializationIcons[std::make_pair(ClassMage, SpecializationWater)] =
        loadTexture(directory + "mage-water.png", database);
    m_specializationIcons[std::make_pair(ClassWarrior, SpecializationBerserker)] =
        loadTexture(directory + "warrior-berserker.png", database);
    m_specializationIcons[std::make_pair(ClassWarrior, SpecializationGuardian)] =
        loadTexture(directory + "warrior-guardian.png", database);
    m_specializationIcons[std::make_pair(ClassRanger, SpecializationSniper)] =
        loadTexture(directory + "ranger-sniper.png", database);
    m_specializationIcons[std::make_pair(ClassRanger, SpecializationScout)] =
        loadTexture(directory + "ranger-scout.png", database);
    m_specializationIcons[std::make_pair(ClassRogue, SpecializationNinja)] =
        loadTexture(directory + "rogue-ninja.png", database);
    m_specializationIcons[std::make_pair(ClassRogue, SpecializationAssassin)] =
        loadTexture(directory + "rogue-assassin.png", database);
    for (std::map<int, plasma::Texture *>::iterator it = m_abilityIcons.begin(); it != m_abilityIcons.end(); it++)
        it->second->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                    plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                    plasma::TEXTURE_WRAP_REPEAT));
    for (std::map<int, plasma::Texture *>::iterator it = m_skillIcons.begin(); it != m_skillIcons.end(); it++)
        it->second->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                    plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                    plasma::TEXTURE_WRAP_REPEAT));
    for (std::map<std::pair<int, int>, plasma::Texture *>::iterator it = m_specializationIcons.begin();
         it != m_specializationIcons.end(); it++)
        it->second->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                    plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                    plasma::TEXTURE_WRAP_REPEAT));

    // The ability bar: six buttons with their cooldown overlays and keys, then the quick item button.
    for (int i = 0; i < AbilityBarQuickItem; i++) {
        plasma::Node *button = abilityButton->clone(m_gameScreen);
        m_abilityButtons.push_back(button);
        plasma::Node *cooldown = m_abilityButtons.back()->findNode(L"cooldown");
        m_cooldowns.push_back(cooldown);
        if (i == AbilityBarMouseLeft) {
            m_abilityButtons.back()->setText(L"info", L"M1", true);
        } else if (i == AbilityBarMouseRight) {
            m_abilityButtons.back()->setText(L"info", L"M2", true);
        } else if (i == AbilityBarQuickItem) {
            m_abilityButtons.back()->setText(L"info", L"Q", true);
        } else {
            std::wstringstream stream;
            stream << i - 1;
            m_abilityButtons.back()->setText(L"info", stream.str(), true);
        }
        m_cooldowns.back()->transformation()->m_deformation.value().scale(1.0f, 0.0f);
        m_cooldowns.back()->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    m_abilityButtons.push_back(quickItemButton);
    plasma::Node *quickItemNode = m_engine->createNode(0, 0, 0, 0);
    m_quickItemWidget = new SpriteWidget(m_engine, quickItemNode, this);
    quickItemButton->moveIntoWidget(L"background", m_quickItemWidget, true);

    // The menu bar: help, skills, crafting, inventory, world map and system. Here i is the stored
    // GameMenuButton index minus one.
    for (int i = GameMenuHelp - 1; i < GameMenuSystem; i++) {
        plasma::Node *button = menuButton->clone(m_gameScreen);
        m_menuButtons.push_back(button);
        plasma::Texture *icon;
        switch (i) {
        case GameMenuSkills - 1:
            icon = loadTexture(directory + "skills.png", database);
            m_menuButtons.back()->setText(L"x");
            break;
        case GameMenuCrafting - 1:
            icon = loadTexture(directory + "crafting.png", database);
            m_menuButtons.back()->setText(L"C");
            break;
        case GameMenuInventory - 1:
            icon = loadTexture(directory + "inventory.png", database);
            m_menuButtons.back()->setText(L"B");
            break;
        case GameMenuWorldMap - 1:
            icon = loadTexture(directory + "worldmap.png", database);
            m_menuButtons.back()->setText(L"M");
            break;
        case GameMenuSystem - 1:
            icon = loadTexture(directory + "system.png", database);
            m_menuButtons.back()->setText(L"O");
            break;
        default:
            icon = loadTexture(directory + "help.png", database);
            m_menuButtons.back()->setText(L"F1");
            break;
        }
        icon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                              plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_NONE,
                                              plasma::TEXTURE_WRAP_NONE));
        int texture = icon->id();
        smoothMeshShape(m_menuButtons.back()->getShape())->m_texture = texture;
        smoothMeshShape(m_menuButtons.back()->getShape())->updateDrawingTextures();
    }

    // The equipment slots, alternately on the left and the right.
    plasma::Node *equipmentBoxLeft = m_gameScreen->findNode(L"equipmentboxleft");
    plasma::Node *equipmentBoxRight = m_gameScreen->findNode(L"equipmentboxright");
    for (int i = 0; i < EquipmentButtonCount; i++) {
        plasma::Node *slotNode = m_engine->createNode(0, 0, 0, 0);
        SpriteWidget *slot = new SpriteWidget(m_engine, slotNode, this);
        plasma::Node *box = (i & 1 ? equipmentBoxLeft : equipmentBoxRight)->clone(m_gameScreen);
        box->moveIntoWidget(L"background", slot, true);
        m_equipmentBoxes.push_back(box);
        m_equipmentSlots.push_back(slot);
    }
    m_engine->deleteNode(equipmentBoxLeft);
    m_engine->deleteNode(equipmentBoxRight);
    m_equipmentBoxes[EquipmentButtonWeaponLeft]->setText(L"Left\nWeapon");
    m_equipmentBoxes[EquipmentButtonWeaponRight]->setText(L"Right\nWeapon");
    m_equipmentBoxes[EquipmentButtonRingLeft]->setText(L"Left Ring");
    m_equipmentBoxes[EquipmentButtonRingRight]->setText(L"Right Ring");
    m_equipmentBoxes[EquipmentButtonNeck]->setText(L"Neck");
    m_equipmentBoxes[EquipmentButtonShoulder]->setText(L"Shoulder");
    m_equipmentBoxes[EquipmentButtonChest]->setText(L"Chest");
    m_equipmentBoxes[EquipmentButtonHands]->setText(L"Hands");
    m_equipmentBoxes[EquipmentButtonFeet]->setText(L"Feet");
    m_equipmentBoxes[EquipmentButtonLamp]->setText(L"Light");
    m_equipmentBoxes[EquipmentButtonSpecial]->setText(L"Special");
    m_equipmentBoxes[EquipmentButtonPet]->setText(L"Pet");
    m_engine->deleteNode(abilityButton);
    m_engine->deleteNode(menuButton);

    // The bars.
    m_experienceBar = m_gameScreen->findNode(L"experiencebar");
    if (m_experienceBar) {
        m_experienceBarFill = m_experienceBar->findNode(L"bar");
        m_experienceBar->transformation()->m_translation =
            plasma::Vector<2, float>(20.0f, 30.0f) - m_experienceBar->transformation()->m_pivot.value();
        m_experienceBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_companionExperienceBar = m_experienceBar->clone(m_gameScreen);
        m_companionExperienceBarFill = m_companionExperienceBar->findNode(L"bar");
    } else {
        m_experienceBarFill = 0;
        m_companionExperienceBar = 0;
    }
    m_ridingBar = m_gameScreen->findNode(L"ridingbar");
    m_manaCubeBar = m_gameScreen->findNode(L"manacubebar");
    m_questBar = m_smallQuestTag->findNode(L"questbar");
    if (m_questBar) {
        m_questBarFill = m_questBar->findNode(L"bar");
        m_questBar->setVisible(false);
    } else {
        m_questBarFill = 0;
    }
    m_lifeBar = m_gameScreen->findNode(L"lifebar");
    if (m_lifeBar) {
        m_lifeBarFill = m_lifeBar->findNode(L"bar");
        m_lifeBar->transformation()->m_translation =
            plasma::Vector<2, float>(20.0f, 125.0f) - m_lifeBar->transformation()->m_pivot.value();
        m_lifeBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_companionLifeBar = m_lifeBar->clone(m_gameScreen);
        m_companionLifeBarFill = m_companionLifeBar->findNode(L"bar");
    } else {
        m_lifeBarFill = 0;
        m_companionLifeBar = 0;
    }
    if (m_lifeBar) {
        m_partyLifeBars.resize(PartyLifeBarCount);
        m_partyLifeBarFills.resize(PartyLifeBarCount);
        for (int i = 0; i < (int)m_partyLifeBars.size(); i++) {
            m_partyLifeBars[i] = m_lifeBar->clone(m_gameScreen);
            m_partyLifeBarFills[i] = m_partyLifeBars[i]->findNode(L"bar");
        }
    }
    m_manaBar = m_gameScreen->findNode(L"mpbar");
    if (m_manaBar) {
        m_manaBarFill = m_manaBar->findNode(L"bar");
        m_chargeBar = m_manaBar->findNode(L"chargebar");
        m_manaBar->transformation()->m_translation =
            plasma::Vector<2, float>(200.0f, 20.0f) - m_manaBar->transformation()->m_pivot.value();
        m_manaBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    } else {
        m_manaBarFill = 0;
    }

    // The life bar templates shown over the creatures.
    m_enemyLifeBar = m_gameScreen->findNode(L"enemylifebar");
    m_enemyLifeBar->setParent(0);
    m_friendLifeBar = m_gameScreen->findNode(L"friendlifebar");
    m_friendLifeBar->setParent(0);
    m_staticLifeBar = m_gameScreen->findNode(L"staticlifebar");
    m_staticLifeBar->setParent(0);
    m_neutralLifeBar = m_gameScreen->findNode(L"neutrallifebar");
    m_neutralLifeBar->setParent(0);
    m_enemyLifeBarSmall = m_gameScreen->findNode(L"enemylifebar:small");
    m_enemyLifeBarSmall->setParent(0);
    m_friendLifeBarSmall = m_gameScreen->findNode(L"friendlifebar:small");
    m_friendLifeBarSmall->setParent(0);
    m_staticLifeBarSmall = m_gameScreen->findNode(L"staticlifebar:small");
    m_staticLifeBarSmall->setParent(0);
    m_neutralLifeBarSmall = m_gameScreen->findNode(L"neutrallifebar:small");
    m_neutralLifeBarSmall->setParent(0);
    m_lifeBars = m_engine->createNode(0, 0, 0, m_gameScreen);
    m_crosshair->setParent(m_gameScreen);
    m_zoomCrosshair->setParent(m_gameScreen);
    m_zoomCrosshair->setVisible(false);

    m_hpBar = m_gameScreen->findNode(L"hpbar");
    if (m_hpBar) {
        m_hpBarFill = m_hpBar->findNode(L"bar");
        m_hpBar->transformation()->m_translation =
            plasma::Vector<2, float>(200.0f, 20.0f) - m_hpBar->transformation()->m_pivot.value();
        m_hpBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    } else {
        m_hpBarFill = 0;
    }
    m_castBar = m_gameScreen->findNode(L"castbar");
    if (m_castBar)
        m_castBarFill = m_castBar->findNode(L"bar");
    else
        m_castBarFill = 0;
    m_staminaBar = m_gameScreen->findNode(L"staminabar");
    if (m_staminaBar)
        m_staminaBarFill = m_staminaBar->findNode(L"bar");
    else
        m_staminaBarFill = 0;
    m_selector = m_gameScreen->findNode(L"selector");
    if (m_previewNode) {
        m_previewWidget = new PreviewWidget(m_engine, m_previewNode, this);
        m_previewNode->setVisible(false);
    } else {
        m_previewWidget = 0;
    }

    // The speech bubbles: four instances of "speech", each with its own display and transformation,
    // and their widgets.
    plasma::Node *speech = m_gameScreen->findNode(L"speech");
    if (speech) {
        m_speechBubbles.resize(4);
        m_speechWidgets.resize(m_speechBubbles.size());
        for (int i = 0; i < (int)m_speechBubbles.size(); i++) {
            m_speechBubbles[i] = speech->instance(m_gameScreen);
            m_speechBubbles[i]->setDisplay(m_speechBubbles[i]->display()->clone());
            m_speechBubbles[i]->setTransformation(m_speechBubbles[i]->transformation()->clone());
            m_speechWidgets[i] = new SpeechWidget(m_engine, m_speechBubbles[i], this);
            m_speechBubbles[i]->setVisible(false);
        }
        m_engine->deleteNode(speech);
    }

    // The dialog, objective and statistics widgets.
    m_dialogNode = m_engine->createNode(0, 0, 0, m_gameScreen);
    m_dialogWidget = new SpeechWidget(m_engine, m_dialogNode, this);
    m_dialogWidget->resizeTo(400.0f, 400.0f, true);
    m_objectiveNode = m_engine->createNode(0, 0, 0, m_engine->root());
    m_objectiveWidget = new ObjectiveWidget(engine, m_objectiveNode, this);
    m_statisticsNode = m_engine->createNode(0, 0, 0, m_engine->root());
    m_statisticsWidget = new StatisticsWidget(engine, m_statisticsNode, this);

    m_cursorNode = m_engine->createNode(0, 0, 0, m_engine->root());
    m_engine->readPlasmaFileIntoNode(L"cursor.plx", m_cursorNode, 0, 0, keys);
    m_cursorNode->setHitTestable(false);

    InitializeCriticalSectionAndSpinCount(&m_streamingLock, 0x80000400);
    InitializeCriticalSectionAndSpinCount(&m_chunkUpdateLock, 0x80000400);
    InitializeCriticalSectionAndSpinCount(&m_mapUpdateLock, 0x80000400);
    InitializeCriticalSectionAndSpinCount(&m_updateLock2, 0x80000400);
    InitializeCriticalSectionAndSpinCount(&m_charactersLock, 0x80000400);

    m_chunks = new Chunk[m_viewDistance * m_viewDistance];

    // The cursor's sprite: one white voxel.
    m_cubeSprite = new Sprite(m_device, false);
    m_cubeSprite->resize(1, 1, 1);
    m_cubeSprite->voxel(0, 0, 0) = plasma::Vector<3, unsigned char>(255, 255, 255);
    m_cubeSprite->updateMesh();

    // The local player.
    m_player = new Creature(1);
    strcpy_s(m_player->m_entityData.name, "Wollay");
    const plasma::Vector<3, float> &spawnPoint = m_world.spawnPoint();
    m_player->m_entityData.position = spawnPoint;
    m_player->m_entityData.rotation = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    m_player->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    m_player->m_entityData.flags = 0;
    m_player->m_entityData.multipliers.damage = 1.0f;
    m_player->m_entityData.level = 1;
    m_player->m_entityData.hp = m_player->maxHP();
    m_player->m_entityData.equipment.weaponRight.category = ItemCategoryWeapon;
    m_player->m_entityData.equipment.weaponRight.id = WeaponSword;
    m_player->m_entityData.equipment.weaponRight.material = ItemMaterialIron;
    m_player->m_entityData.equipment.weaponLeft.category = ItemCategoryWeapon;
    m_player->m_entityData.equipment.weaponLeft.id = WeaponSword;
    m_player->m_entityData.equipment.weaponLeft.material = ItemMaterialIron;
    m_player->m_entityData.equipment.chest.material = ItemMaterialIron;
    m_player->m_entityData.hostilityType = HostilityPlayer;
    m_player->m_entityData.classType = ClassWarrior;
    m_player->m_entityData.origin = spawnPoint;
    m_player->m_entityData.race = RaceHumanMale;
    m_player->m_playerData = new Creature::PlayerData;
    fillAppearance(m_player->m_entityData.race, m_player->m_entityData.appearance, m_player->m_playerData);
    m_world.m_creatures[1] = m_player;
    m_world.setLocalPlayer(m_player);

    // The lights' textures (256 x 256): a soft white disc and a glow. The textures' curves: the disc's
    // alpha falls with the squared distance from the centre (cubed, at most 128); the glow's with the
    // distance, to the fourth power, its red from 100 to 255.
    m_device->CreateTexture(256, 256, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m_shadowTexture, 0);
    D3DLOCKED_RECT rect;
    m_shadowTexture->LockRect(0, &rect, 0, D3DLOCK_DISCARD);
    unsigned char *row = (unsigned char *)rect.pBits;
    for (int y = 0; y < 256; y++) {
        plasma::Vector<4, unsigned char> *pixel = (plasma::Vector<4, unsigned char> *)row;
        for (int x = 0; x < 256; x++) {
            float value = 1.0f - plasma::Vector<2, float>(x - 127.5f, y - 127.5f).squaredLength() / 16384.0f;
            if (value < 0.0f)
                value = 0.0f;
            pixel[x] = plasma::Vector<4, unsigned char>(255, 255, 255, (unsigned char)(value * value * value * 128.0f));
        }
        row += rect.Pitch;
    }
    m_shadowTexture->UnlockRect(0);
    m_device->CreateTexture(256, 256, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m_sunTexture, 0);
    m_sunTexture->LockRect(0, &rect, 0, D3DLOCK_DISCARD);
    row = (unsigned char *)rect.pBits;
    for (int y = 0; y < 256; y++) {
        plasma::Vector<4, unsigned char> *pixel = (plasma::Vector<4, unsigned char> *)row;
        for (int x = 0; x < 256; x++) {
            float value = 1.05 - plasma::Vector<2, float>(x - 127.5f, y - 127.5f).length() * 1.05f / 128.0f;
            if (value < 0.0f)
                value = 0.0f;
            else if (value > 1.0f)
                value = 1.0f;
            value = value * value * value * value;
            pixel[x] = plasma::Vector<4, unsigned char>((unsigned char)(value * 155.0f + 100.0f), 255, 255,
                                                        (unsigned char)(value * 255.0f));
        }
        row += rect.Pitch;
    }
    m_sunTexture->UnlockRect(0);

    // Sprites of the world's sprite manager by slot (the ids are sprite manager slots; their names are
    // in SpriteManager::loadSprites).
    m_plantSprites.resize(PlantSpriteSlots, 0);
    m_plantSprites[PlantFlowers2] = m_world.m_spriteManager.sprite(SpriteFlowers2);
    m_plantSprites[PlantFlowers] = m_world.m_spriteManager.sprite(SpriteFlowers);
    m_plantSprites[PlantGrass] = m_world.m_spriteManager.sprite(SpriteGrass);
    m_plantSprites[PlantGrass2] = m_world.m_spriteManager.sprite(SpriteGrass2);
    m_plantSprites[PlantGrass3] = m_world.m_spriteManager.sprite(SpriteGrass3);
    m_plantSprites[PlantCoral] = m_world.m_spriteManager.sprite(SpriteCoral);
    m_plantSprites[PlantUnderwaterPlant] = m_world.m_spriteManager.sprite(SpriteUnderwaterPlant);
    m_plantSprites[PlantAlga] = m_world.m_spriteManager.sprite(SpriteAlga);
    m_plantSprites[PlantLavaFlower] = m_world.m_spriteManager.sprite(SpriteLavaFlower);
    m_plantSprites[PlantLavaGrass] = m_world.m_spriteManager.sprite(SpriteLavaGrass);
    m_plantSprites[PlantThornPlant] = m_world.m_spriteManager.sprite(SpriteThornPlant);
    m_plantSprites[PlantEchinacea2] = m_world.m_spriteManager.sprite(SpriteEchinacea2);
    m_plantSprites[PlantLeaf] = m_world.m_spriteManager.sprite(SpriteLeaf);
    m_plantSprites[PlantLantern02] = m_world.m_spriteManager.sprite(SpriteLantern02);
    m_plantSprites[PlantPlantTorch] = m_world.m_spriteManager.sprite(SpritePlantTorch);
    m_plantSprites[PlantStone] = m_world.m_spriteManager.sprite(SpriteStone);
    m_plantSprites[PlantStone2] = m_world.m_spriteManager.sprite(SpriteStone2);
    m_plantSprites[PlantTendril] = m_world.m_spriteManager.sprite(SpriteTendril);
    m_plantSprites[PlantTulipsColorful] = m_world.m_spriteManager.sprite(SpriteTulipsColorful);
    m_plantSprites[PlantCornflower] = m_world.m_spriteManager.sprite(SpriteCornflower);
    m_plantSprites[PlantReed] = m_world.m_spriteManager.sprite(SpriteReed);
    m_plantSprites[PlantPumpkinLeaves] = m_world.m_spriteManager.sprite(SpritePumpkinLeaves);
    m_plantSprites[PlantPineappleLeaves] = m_world.m_spriteManager.sprite(SpritePineappleLeaves);
    m_plantSprites[PlantSunflower] = m_world.m_spriteManager.sprite(SpriteSunflower);
    m_plantSprites[PlantBeanTendril] = m_world.m_spriteManager.sprite(SpriteBeanTendril);
    m_plantSprites[PlantDesertFlower01] = m_world.m_spriteManager.sprite(SpriteDesertFlower01);
    m_plantSprites[PlantDesertFlower02] = m_world.m_spriteManager.sprite(SpriteDesertFlower02);
    m_plantSprites[PlantWheat] = m_world.m_spriteManager.sprite(SpriteWheat);
    m_plantSprites[PlantCorn] = m_world.m_spriteManager.sprite(SpriteCorn);
    m_plantSprites[PlantWaterLily01] = m_world.m_spriteManager.sprite(SpriteWaterLily01);
    m_plantSprites[PlantWaterLily02] = m_world.m_spriteManager.sprite(SpriteWaterLily02);
    m_plantSprites[PlantInnSign] = m_world.m_spriteManager.sprite(SpriteInnSign);
    m_plantSprites[PlantIdentifierSign] = m_world.m_spriteManager.sprite(SpriteIdentifierSign);
    m_plantSprites[PlantShopSign] = m_world.m_spriteManager.sprite(SpriteShopSign);
    m_plantSprites[PlantWeaponShopSign] = m_world.m_spriteManager.sprite(SpriteWeaponShopSign);
    m_plantSprites[PlantArmorShopSign] = m_world.m_spriteManager.sprite(SpriteArmorShopSign);
    m_plantSprites[PlantSmithySign] = m_world.m_spriteManager.sprite(SpriteSmithySign);
    m_plantSprites[PlantCarpentersshopSign] = m_world.m_spriteManager.sprite(SpriteCarpentersshopSign);
    m_plantSprites[PlantWeavingmillSign] = m_world.m_spriteManager.sprite(SpriteWeavingmillSign);
    m_plantSprites[PlantChristmasTree] = m_world.m_spriteManager.sprite(SpriteChristmasTree);
    m_plantSprites[PlantIvy] = m_world.m_spriteManager.sprite(SpriteIvy);
    m_plantSprites[PlantWallRosesRed] = m_world.m_spriteManager.sprite(SpriteWallRosesRed);
    m_plantSprites[PlantWallRosesWhite] = m_world.m_spriteManager.sprite(SpriteWallRosesWhite);
    m_plantSprites[PlantIncaArt1] = m_world.m_spriteManager.sprite(SpriteIncaArt1);
    m_plantSprites[PlantIncaArt2] = m_world.m_spriteManager.sprite(SpriteIncaArt2);
    m_plantSprites[PlantIncaArt3] = m_world.m_spriteManager.sprite(SpriteIncaArt3);
    m_plantSprites[PlantIncaArt4] = m_world.m_spriteManager.sprite(SpriteIncaArt4);
    m_plantSprites[PlantCrest1] = m_world.m_spriteManager.sprite(SpriteCrest1);
    m_plantSprites[PlantCastleChain] = m_world.m_spriteManager.sprite(SpriteCastleChain);
    m_plantSprites[PlantWallSkull] = m_world.m_spriteManager.sprite(SpriteWallSkull);
    m_plantSprites[PlantTorchRead] = m_world.m_spriteManager.sprite(SpriteTorchRead);
    m_plantSprites[PlantTorchGreen] = m_world.m_spriteManager.sprite(SpriteTorchGreen);
    m_plantSprites[PlantTorchBlue] = m_world.m_spriteManager.sprite(SpriteTorchBlue);
    m_plantSprites[PlantTorchYellow] = m_world.m_spriteManager.sprite(SpriteTorchYellow);
    m_plantSprites[PlantLiana] = m_world.m_spriteManager.sprite(SpriteLiana);
    m_plantSprites[PlantChandelier] = m_world.m_spriteManager.sprite(SpriteChandelier);
    m_plantSprites[PlantCobwebs] = m_world.m_spriteManager.sprite(SpriteCobwebs);
    m_plantSprites[PlantCobwebs2] = m_world.m_spriteManager.sprite(SpriteCobwebs2);
    m_staticEntitySprites.resize(StaticObjectTypeCount, 0);
    m_staticEntitySprites[StaticObjectStatue] = m_world.m_spriteManager.sprite(SpriteObjectGoddess);
    m_staticEntitySprites[StaticObjectDoor] = m_world.m_spriteManager.sprite(SpriteObjectDoor);
    m_staticEntitySprites[StaticObjectBigDoor] = m_world.m_spriteManager.sprite(SpriteBigDoor);
    m_staticEntitySprites[StaticObjectWindow] = m_world.m_spriteManager.sprite(SpriteObjectWindow);
    m_staticEntitySprites[StaticObjectCastleWindow] = m_world.m_spriteManager.sprite(SpriteCastleWindow);
    m_staticEntitySprites[StaticObjectGate] = m_world.m_spriteManager.sprite(SpriteGate);
    m_staticEntitySprites[StaticObjectSpikeTrap] = m_world.m_spriteManager.sprite(SpriteSpikeTrap);
    m_staticEntitySprites[StaticObjectStompTrap] = m_world.m_spriteManager.sprite(SpriteStompTrap);
    m_staticEntitySprites[StaticObjectLever] = m_world.m_spriteManager.sprite(SpriteLever);
    m_staticEntitySprites[StaticObjectChest] = m_world.m_spriteManager.sprite(SpriteChestBase02);
    m_staticEntitySprites[StaticObjectChestLid] = m_world.m_spriteManager.sprite(SpriteChestTop02);
    m_staticEntitySprites[StaticObjectTable] = m_world.m_spriteManager.sprite(SpriteTable);
    m_staticEntitySprites[StaticObjectStoneTable] = m_world.m_spriteManager.sprite(SpriteStoneTable);
    m_staticEntitySprites[StaticObjectStoneStool] = m_world.m_spriteManager.sprite(SpriteStoneStool);
    m_staticEntitySprites[StaticObjectStool] = m_world.m_spriteManager.sprite(SpriteStool);
    m_staticEntitySprites[StaticObjectSandstoneStool] = m_world.m_spriteManager.sprite(SpriteSandstoneStool);
    m_staticEntitySprites[StaticObjectBench] = m_world.m_spriteManager.sprite(SpriteBench);
    m_staticEntitySprites[StaticObjectBed] = m_world.m_spriteManager.sprite(SpriteBed);
    m_staticEntitySprites[StaticObjectBedTable] = m_world.m_spriteManager.sprite(SpriteBedtable);
    m_staticEntitySprites[StaticObjectMarketStand1] = m_world.m_spriteManager.sprite(SpriteMarketStand1);
    m_staticEntitySprites[StaticObjectMarketStand2] = m_world.m_spriteManager.sprite(SpriteMarketStand2);
    m_staticEntitySprites[StaticObjectMarketStand3] = m_world.m_spriteManager.sprite(SpriteMarketStand3);
    m_staticEntitySprites[StaticObjectBarrel] = m_world.m_spriteManager.sprite(SpriteBarrel);
    m_staticEntitySprites[StaticObjectCrate] = m_world.m_spriteManager.sprite(SpriteCrate);
    m_staticEntitySprites[StaticObjectOpenCrate] = m_world.m_spriteManager.sprite(SpriteOpenCrate);
    m_staticEntitySprites[StaticObjectSack] = m_world.m_spriteManager.sprite(SpriteSack);
    m_staticEntitySprites[StaticObjectShelter] = m_world.m_spriteManager.sprite(SpriteShelter);
    m_staticEntitySprites[StaticObjectCupboard] = m_world.m_spriteManager.sprite(SpriteCupboard);
    m_staticEntitySprites[StaticObjectDesktop] = m_world.m_spriteManager.sprite(SpriteDesktop);
    m_staticEntitySprites[StaticObjectCounter] = m_world.m_spriteManager.sprite(SpriteCounter);
    m_staticEntitySprites[StaticObjectShelf1] = m_world.m_spriteManager.sprite(SpriteShelf1);
    m_staticEntitySprites[StaticObjectShelf2] = m_world.m_spriteManager.sprite(SpriteShelf2);
    m_staticEntitySprites[StaticObjectShelf3] = m_world.m_spriteManager.sprite(SpriteShelf3);
    m_staticEntitySprites[StaticObjectCastleShelf1] = m_world.m_spriteManager.sprite(SpriteCastleShelf1);
    m_staticEntitySprites[StaticObjectCastleShelf2] = m_world.m_spriteManager.sprite(SpriteCastleShelf2);
    m_staticEntitySprites[StaticObjectCastleShelf3] = m_world.m_spriteManager.sprite(SpriteCastleShelf3);
    m_staticEntitySprites[StaticObjectStoneShelf1] = m_world.m_spriteManager.sprite(SpriteStoneShelf1);
    m_staticEntitySprites[StaticObjectStoneShelf2] = m_world.m_spriteManager.sprite(SpriteStoneShelf2);
    m_staticEntitySprites[StaticObjectStoneShelf3] = m_world.m_spriteManager.sprite(SpriteStoneShelf3);
    m_staticEntitySprites[StaticObjectSandstoneShelf1] = m_world.m_spriteManager.sprite(SpriteSandstoneShelf1);
    m_staticEntitySprites[StaticObjectSandstoneShelf2] = m_world.m_spriteManager.sprite(SpriteSandstoneShelf2);
    m_staticEntitySprites[StaticObjectSandstoneShelf3] = m_world.m_spriteManager.sprite(SpriteSandstoneShelf3);
    m_staticEntitySprites[StaticObjectCorpse] = m_world.m_spriteManager.sprite(SpriteCorpse);
    m_staticEntitySprites[StaticObjectRuneStone] = m_world.m_spriteManager.sprite(SpriteRunestone);
    m_staticEntitySprites[StaticObjectArtifact] = m_world.m_spriteManager.sprite(SpriteArtifact);
    m_staticEntitySprites[StaticObjectFlowerBox1] = m_world.m_spriteManager.sprite(SpriteFlowerBox01);
    m_staticEntitySprites[StaticObjectFlowerBox2] = m_world.m_spriteManager.sprite(SpriteFlowerBox02);
    m_staticEntitySprites[StaticObjectFlowerBox3] = m_world.m_spriteManager.sprite(SpriteFlowerBox03);
    m_staticEntitySprites[StaticObjectStreetLight] = m_world.m_spriteManager.sprite(SpriteStreetLight02);
    m_staticEntitySprites[StaticObjectFireStreetLight] = m_world.m_spriteManager.sprite(SpriteStreetLight01);
    m_staticEntitySprites[StaticObjectVase1] = m_world.m_spriteManager.sprite(SpriteFlowerVase1);
    m_staticEntitySprites[StaticObjectVase2] = m_world.m_spriteManager.sprite(SpriteFlowerVase2);
    m_staticEntitySprites[StaticObjectVase3] = m_world.m_spriteManager.sprite(SpriteFlowerVase3);
    m_staticEntitySprites[StaticObjectVase4] = m_world.m_spriteManager.sprite(SpriteFlowerVase4);
    m_staticEntitySprites[StaticObjectVase5] = m_world.m_spriteManager.sprite(SpriteFlowerVase5);
    m_staticEntitySprites[StaticObjectVase6] = m_world.m_spriteManager.sprite(SpriteFlowerVase6);
    m_staticEntitySprites[StaticObjectVase7] = m_world.m_spriteManager.sprite(SpriteFlowerVase7);
    m_staticEntitySprites[StaticObjectVase8] = m_world.m_spriteManager.sprite(SpriteFlowerVase8);
    m_staticEntitySprites[StaticObjectVase9] = m_world.m_spriteManager.sprite(SpriteFlowerVase9);
    m_staticEntitySprites[StaticObjectFence1] = m_world.m_spriteManager.sprite(SpriteFence01);
    m_staticEntitySprites[StaticObjectFence2] = m_world.m_spriteManager.sprite(SpriteFence02);
    m_staticEntitySprites[StaticObjectFence3] = m_world.m_spriteManager.sprite(SpriteFence03);
    m_staticEntitySprites[StaticObjectFence4] = m_world.m_spriteManager.sprite(SpriteFence04);
    m_staticEntitySprites[StaticObjectFurnace] = m_world.m_spriteManager.sprite(SpriteFurnace);
    m_staticEntitySprites[StaticObjectAnvil] = m_world.m_spriteManager.sprite(SpriteAnvil);
    m_staticEntitySprites[StaticObjectSaw] = m_world.m_spriteManager.sprite(SpriteSawbench);
    m_staticEntitySprites[StaticObjectWorkbench] = m_world.m_spriteManager.sprite(SpriteWorkbench);
    m_staticEntitySprites[StaticObjectCustomizationBench] = m_world.m_spriteManager.sprite(SpriteCustomizationBench);
    m_staticEntitySprites[StaticObjectSpinningWheel] = m_world.m_spriteManager.sprite(SpriteSpinningwheel);
    m_staticEntitySprites[StaticObjectLoom] = m_world.m_spriteManager.sprite(SpriteLoom);
    m_staticEntitySprites[StaticObjectCampfire] = m_world.m_spriteManager.sprite(SpriteCampfire);
    m_staticEntitySprites[StaticObjectTent] = m_world.m_spriteManager.sprite(SpriteTent);
    m_staticEntitySprites[StaticObjectBeachUmbrella] = m_world.m_spriteManager.sprite(SpriteBeachUmbrella);
    m_staticEntitySprites[StaticObjectBeachTowel] = m_world.m_spriteManager.sprite(SpriteBeachTowel);
    m_staticEntitySprites[StaticObjectSleepingMat] = m_world.m_spriteManager.sprite(SpriteWoodMat);

    m_buildCursorSprite = new Sprite(m_device, false);
    m_buildCursorSprite->load("build-cursor.cub", 0, true);

    m_running = true;
    // The chunk thread: keeps the chunks of the grid around m_chunkOrigin loaded, nearest first,
    // within a memory budget set by the render distance, meshes at most one chunk per pass, and saves
    // the character once a minute in a local world.
    m_chunkThread = StartThread([this]() {
        static int lastSave;
        timeGetTime(); // the result is not used
        while (m_running) {
            EnterCriticalSection(&m_streamingLock);
            plasma::Vector<2, int> origin = m_chunkOrigin;
            bool worldChanged = m_seed != m_world.getSeed() || m_worldName != m_world.getName();
            LeaveCriticalSection(&m_streamingLock);
            if (worldChanged) {
                EnterCriticalSection(&m_chunkUpdateLock);
                for (int i = 0; i < m_viewDistance * m_viewDistance; i++)
                    m_chunks[i].reset();
                LeaveCriticalSection(&m_chunkUpdateLock);
            } else {
                std::vector<NearbyChunk> slots;
                for (int x = origin[0]; x < origin[0] + m_viewDistance; x++) {
                    for (int y = origin[1]; y < origin[1] + m_viewDistance; y++) {
                        if (x >= 0 && y >= 0 && x < ChunksPerWorld && y < ChunksPerWorld) {
                            plasma::Vector<2, int> zone(x, y);
                            plasma::Vector<2, int> offset(x - m_viewDistance / 2 - origin[0],
                                                          y - m_viewDistance / 2 - origin[1]);
                            int distance = offset.squaredLength();
                            NearbyChunk slot;
                            slot.zone = zone;
                            slot.distance = distance;
                            slot.chunk = &m_chunks[(y % m_viewDistance) * m_viewDistance + x % m_viewDistance];
                            slots.push_back(slot);
                        }
                    }
                }
                std::sort(slots.begin(), slots.end(),
                          [](const NearbyChunk &a, const NearbyChunk &b) { return a.distance < b.distance; });
                long long used = 0;
                m_world.enterRegions();
                long long budget = ChunkVertexBudget;
                budget = (long long)(m_options.renderDistance * 0.01 * budget);
                m_world.leaveRegions();
                bool mayMesh = true;
                EnterCriticalSection(&m_chunkUpdateLock);
                for (std::vector<NearbyChunk>::iterator slot = slots.begin(); slot != slots.end(); ++slot) {
                    if (used > budget) {
                        slot->chunk->reset();
                    } else {
                        if (slot->chunk->position != slot->zone || slot->chunk->needsRemesh) {
                            if (slot->chunk->position != slot->zone)
                                slot->chunk->reset();
                            if (mayMesh && canMesh(slot->zone[0], slot->zone[1])) {
                                mayMesh = false;
                                updateChunk(slot->zone[0], slot->zone[1]);
                            }
                        }
                        used += slot->chunk->vertexCount;
                    }
                }
                LeaveCriticalSection(&m_chunkUpdateLock);
            }
            int now = timeGetTime();
            if (now - lastSave > 60000 && !m_world.getName().empty() && m_running) {
                saveCharacter(m_selectedCharacter, m_player);
                lastSave = now;
            }
            Sleep(ChunkThreadSleep);
        }
    });
    SetThreadPriority(m_chunkThread, THREAD_PRIORITY_BELOW_NORMAL);
    // The map thread: loads the regions and the landscapes of the 7 x 7 regions around the zone it
    // centres on, then, ten times, the nearest zone within 10 whose map tile is missing or out of
    // date, and unloads distant tiles once a second.
    m_mapThread = StartThread([this]() {
        int lastUnload = timeGetTime();
        while (m_running) {
            EnterCriticalSection(&m_mapUpdateLock);
            EnterCriticalSection(&m_streamingLock);
            plasma::Vector<2, int> base = m_cameraZone;
            plasma::Vector<2, int> centre((int)(m_mapOffset[0] / float(BlocksPerZone) + base[0]),
                                          (int)(m_mapOffset[1] / float(BlocksPerZone) + base[1]));
            bool stop = m_seed != m_world.getSeed() || m_worldName != m_world.getName() || !m_running;
            LeaveCriticalSection(&m_streamingLock);
            if (!stop) {
                for (int x = centre[0] / ZonesPerRegion - 3; x <= centre[0] / ZonesPerRegion + 3; x++) {
                    for (int y = centre[1] / ZonesPerRegion - 3; y <= centre[1] / ZonesPerRegion + 3; y++) {
                        EnterCriticalSection(&m_streamingLock);
                        stop = m_seed != m_world.getSeed() || m_worldName != m_world.getName();
                        LeaveCriticalSection(&m_streamingLock);
                        if (stop)
                            break;
                        m_world.enterZones();
                        m_worldMap.loadRegion(x, y);
                        m_world.leaveZones();
                    }
                    if (stop)
                        break;
                }
                for (int x = centre[0] / ZonesPerRegion - 3; x <= centre[0] / ZonesPerRegion + 3; x++) {
                    for (int y = centre[1] / ZonesPerRegion - 3; y <= centre[1] / ZonesPerRegion + 3; y++) {
                        EnterCriticalSection(&m_streamingLock);
                        stop = m_seed != m_world.getSeed() || m_worldName != m_world.getName();
                        LeaveCriticalSection(&m_streamingLock);
                        if (stop)
                            break;
                        m_world.enterZones();
                        m_worldMap.loadLandscape(x, y);
                        m_world.leaveZones();
                    }
                    if (stop)
                        break;
                }
                for (int pass = 0; pass < 10; pass++) {
                    int nearestDistance = -1;
                    plasma::Vector<2, int> nearest(-1, -1);
                    for (int x = centre[0] - 10; x < centre[0] + 10; x++) {
                        for (int y = centre[1] - 10; y < centre[1] + 10; y++) {
                            m_world.enterZones();
                            m_worldMap.enterRegions();
                            // No tile yet, or a tile without a current model that is built or whose zone is loaded.
                            ZoneTile *tile = m_worldMap.getZoneTile(x, y);
                            bool missing = !tile || !(tile->m_sprite && !tile->m_needsRebuild) &&
                                                        ((tile->m_flags & (1 << ZoneTileBuilt)) || m_world.zone(x, y));
                            m_worldMap.leaveRegions();
                            m_world.leaveZones();
                            if (missing) {
                                int distance = (plasma::Vector<2, int>(x, y) - centre).squaredLength();
                                if (nearestDistance < 0 || distance < nearestDistance) {
                                    nearestDistance = distance;
                                    nearest = plasma::Vector<2, int>(x, y);
                                }
                            }
                        }
                    }
                    if (nearestDistance >= 0) {
                        m_world.enterZones();
                        m_worldMap.loadZone(nearest[0], nearest[1]);
                        m_world.leaveZones();
                    }
                }
            }
            int now = timeGetTime();
            if (now - lastUnload > 1000) {
                lastUnload = now;
                m_worldMap.cleanUp(centre[0], centre[1]);
            }
            LeaveCriticalSection(&m_mapUpdateLock);
            Sleep(MapThreadSleep);
        }
    });
    SetThreadPriority(m_mapThread, THREAD_PRIORITY_BELOW_NORMAL);
    m_zoneGenerationRunning = true;
    // The zone thread: generates the nearest missing zone within 3 zones of a requested one (and
    // within 3 zones of the camera), and once a second (at once when the world changed) unloads the
    // zones, regions and records far from every request; after a world change it sets the world up
    // again, adds it to the saved worlds and places the player.
    m_zoneGenerationThread = StartThread([this]() {
        int lastCleanup = timeGetTime();
        while (m_zoneGenerationRunning) {
            EnterCriticalSection(&m_streamingLock);
            bool worldChanged = m_seed != m_world.getSeed() || m_worldName != m_world.getName();
            plasma::Vector<2, int> origin = m_chunkOrigin;
            plasma::Vector<2, int> camera = m_cameraBlock;
            std::list<plasma::Vector<2, int>> requests = m_playerZones;
            LeaveCriticalSection(&m_streamingLock);
            if (worldChanged) {
                EnterCriticalSection(&m_chunkUpdateLock);
                for (int x = origin[0]; x < origin[0] + m_viewDistance; x++) {
                    for (int y = origin[1]; y < origin[1] + m_viewDistance; y++) {
                        if (x >= 0 && y >= 0 && x < ChunksPerWorld && y < ChunksPerWorld)
                            m_chunks[(y % m_viewDistance) * m_viewDistance + x % m_viewDistance].reset();
                    }
                }
                LeaveCriticalSection(&m_chunkUpdateLock);
            } else {
                bool found = false;
                int nearestDistance = 0x90000; // 3 zones (768 blocks), squared
                plasma::Vector<2, int> nearest;
                for (std::list<plasma::Vector<2, int>>::iterator it = requests.begin(); it != requests.end(); it++) {
                    plasma::Vector<2, int> zone = *it;
                    int minX = zone[0] - 3;
                    int maxX = zone[0] + 3;
                    int minY = zone[1] - 3;
                    int maxY = zone[1] + 3;
                    if (minX < 0)
                        minX = 0;
                    if (maxX >= ZonesPerWorld)
                        maxX = ZonesPerWorld - 1;
                    if (minY < 0)
                        minY = 0;
                    if (maxY >= ZonesPerWorld)
                        maxY = ZonesPerWorld - 1;
                    for (int x = minX; x <= maxX; x++) {
                        for (int y = minY; y <= maxY; y++) {
                            int distance = (camera - plasma::Vector<2, int>(x * BlocksPerZone + BlocksPerZone / 2,
                                                                            y * BlocksPerZone + BlocksPerZone / 2))
                                               .squaredLength();
                            if (distance < nearestDistance && !m_world.zone(x, y)) {
                                nearest = plasma::Vector<2, int>(x, y);
                                nearestDistance = distance;
                                found = true;
                            }
                        }
                    }
                }
                if (found) {
                    m_world.generateZone(nearest[0], nearest[1]);
                    m_worldMap.enterZones();
                    ZoneTile *tile = m_worldMap.getZoneTile(nearest[0], nearest[1]);
                    if (tile)
                        tile->m_needsRebuild = true;
                    m_worldMap.leaveZones();
                    EnterCriticalSection(&m_streamingLock);
                    LeaveCriticalSection(&m_streamingLock);
                }
            }
            int now = timeGetTime();
            if (worldChanged || now - lastCleanup > 1000) {
                lastCleanup = now;
                for (int x = 0; x < 0x400; x++) {
                    for (int y = 0; y < 0x400; y++) {
                        Region *region = m_world.getRegion(x, y);
                        if (!region)
                            continue;
                        // Zones farther than 4 zones from every request.
                        for (int i = 0; i < 64; i++) {
                            for (int j = 0; j < 64; j++) {
                                Zone *zone = region->m_zones[i][j];
                                if (!zone)
                                    continue;
                                bool keep = false;
                                if (!worldChanged) {
                                    for (std::list<plasma::Vector<2, int>>::iterator it = requests.begin();
                                         it != requests.end(); it++) {
                                        if ((zone->m_position - *it).squaredLength() < 16) {
                                            keep = true;
                                            break;
                                        }
                                    }
                                }
                                if (!keep)
                                    m_world.removeZone(zone->m_position[0], zone->m_position[1]);
                            }
                        }
                        // The region farther than 2 regions, its record farther than 4, from every request.
                        bool keep = false;
                        if (!worldChanged) {
                            for (std::list<plasma::Vector<2, int>>::iterator it = requests.begin();
                                 it != requests.end(); it++) {
                                if (abs(x - (*it)[0] / ZonesPerRegion) <= 2 &&
                                    abs(y - (*it)[1] / ZonesPerRegion) <= 2) {
                                    keep = true;
                                    break;
                                }
                            }
                        }
                        if (!keep)
                            m_world.removeRegion(x, y);
                        keep = false;
                        if (!worldChanged) {
                            for (std::list<plasma::Vector<2, int>>::iterator it = requests.begin();
                                 it != requests.end(); it++) {
                                if (abs(x - (*it)[0] / ZonesPerRegion) <= 4 &&
                                    abs(y - (*it)[1] / ZonesPerRegion) <= 4) {
                                    keep = true;
                                    break;
                                }
                            }
                        }
                        if (!keep)
                            m_world.removeRecord(x, y);
                    }
                }
                if (worldChanged) {
                    m_world.enterRegions();
                    m_world.enterZones();
                    m_world.setup(m_seed, m_worldName);
                    m_worldMap.open();
                    if (!m_worldName.empty()) {
                        // Select the world among the saved ones, or save it as a new one.
                        m_selectedWorld = -1;
                        for (int i = 0; i < (int)m_worlds.size(); i++) {
                            if (m_worlds[i]->m_name == m_worldName && m_worlds[i]->m_seed == m_seed) {
                                m_selectedWorld = i;
                                break;
                            }
                        }
                        if (m_selectedWorld < 0) {
                            BytesIO bytes;
                            int count = (int)m_worlds.size() + 1;
                            bytes.write(&count, sizeof(count));
                            m_worldsDatabase.write("num", bytes);
                            WorldInfo *world = new WorldInfo;
                            world->m_seed = m_seed;
                            world->m_name = m_worldName;
                            m_worlds.push_back(world);
                            m_selectedWorld = (int)m_worlds.size() - 1;
                            saveWorldInfo(m_selectedWorld, world, false);
                            updateWorldList();
                        }
                    }
                    // The player starts where it left this world, or at the spawn point.
                    if (m_player->m_playerData->worldPositions.find(std::make_pair(m_seed, m_worldName)) ==
                        m_player->m_playerData->worldPositions.end())
                        m_player->m_entityData.position = m_world.spawnPoint();
                    else
                        m_player->m_entityData.position =
                            m_player->m_playerData->worldPositions[std::make_pair(m_seed, m_worldName)];
                    m_player->m_drawnPosition = m_player->m_entityData.position;
                    m_player->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                    m_world.leaveZones();
                    m_world.leaveRegions();
                }
            }
            Sleep(ZoneThreadSleep);
        }
    });

    // The player's inventory has four tabs of 50 slots (the first keeps a larger size).
    m_player->m_inventory.m_inventoryTabs.resize(InventoryTabCount);
    if (m_player->m_inventory.m_inventoryTabs[InventoryTabEquipment].size() < 50)
        m_player->m_inventory.m_inventoryTabs[InventoryTabEquipment].resize(50);
    m_player->m_inventory.m_inventoryTabs[InventoryTabGeneral].resize(50);
    m_player->m_inventory.m_inventoryTabs[InventoryTabMaterials].resize(50);
    m_player->m_inventory.m_inventoryTabs[InventoryTabPets].resize(50);

    // The night sky: 3000 stars on the upper half sphere of radius 100, each with a size (0.01 to
    // 0.11), one in ten twice as large.
    m_stars.resize(3000);
    for (int i = 0; i < (int)m_stars.size(); i++) {
        float yaw = rand() * 6.283185307179586 / RAND_MAX;
        float pitch = rand() * 1.5707963267948966 / RAND_MAX;
        float horizontal = cos(pitch) * 100.0f;
        m_stars[i] = plasma::Vector<4, float>(cos(yaw) * horizontal, sin(yaw) * horizontal, sin(pitch) * 100.0f,
                                              rand() * 0.1f / RAND_MAX + 0.01f);
        if (rand() % 10 == 0)
            m_stars[i][3] *= 2.0f;
    }

    // The trade, crafting and inventory tab icons.
    plasma::Texture *buyIcon = loadTexture(directory + "buy.png", database);
    buyIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                             plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                             plasma::TEXTURE_WRAP_REPEAT));
    plasma::Texture *buyBackIcon = loadTexture(directory + "buy-back.png", database);
    buyBackIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                 plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                 plasma::TEXTURE_WRAP_REPEAT));
    m_craftHeavyArmorIcon = loadTexture(directory + "craft-heavy-armor.png", database);
    m_craftHeavyArmorIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                           plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                           plasma::TEXTURE_WRAP_REPEAT));
    m_craftMediumArmorIcon = loadTexture(directory + "craft-medium-armor.png", database);
    m_craftMediumArmorIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                            plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                            plasma::TEXTURE_WRAP_REPEAT));
    m_craftRogueArmorIcon = loadTexture(directory + "craft-rogue-armor.png", database);
    m_craftRogueArmorIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                           plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                           plasma::TEXTURE_WRAP_REPEAT));
    m_craftLightArmorIcon = loadTexture(directory + "craft-light-armor.png", database);
    m_craftLightArmorIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                           plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                           plasma::TEXTURE_WRAP_REPEAT));
    m_craftMeleeWeaponsIcon = loadTexture(directory + "craft-melee-weapons.png", database);
    m_craftMeleeWeaponsIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                             plasma::TEXTURE_FILTER_NEAREST,
                                                             plasma::TEXTURE_WRAP_REPEAT, plasma::TEXTURE_WRAP_REPEAT));
    m_craftRogueWeaponsIcon = loadTexture(directory + "craft-rogue-weapons.png", database);
    m_craftRogueWeaponsIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                             plasma::TEXTURE_FILTER_NEAREST,
                                                             plasma::TEXTURE_WRAP_REPEAT, plasma::TEXTURE_WRAP_REPEAT));
    m_craftRangedWeaponsIcon = loadTexture(directory + "craft-ranged-weapons.png", database);
    m_craftRangedWeaponsIcon->setFormat(
        plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_FILTER_NEAREST,
                              plasma::TEXTURE_WRAP_REPEAT, plasma::TEXTURE_WRAP_REPEAT));
    m_craftMagicWeaponsIcon = loadTexture(directory + "craft-magic-weapons.png", database);
    m_craftMagicWeaponsIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                             plasma::TEXTURE_FILTER_NEAREST,
                                                             plasma::TEXTURE_WRAP_REPEAT, plasma::TEXTURE_WRAP_REPEAT));
    m_craftAmuletsIcon = loadTexture(directory + "craft-amulets.png", database);
    m_craftAmuletsIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                        plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                        plasma::TEXTURE_WRAP_REPEAT));
    m_craftCookingIcon = loadTexture(directory + "craft-cooking.png", database);
    m_craftCookingIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                        plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                        plasma::TEXTURE_WRAP_REPEAT));
    m_craftAlchemyIcon = loadTexture(directory + "craft-alchemy.png", database);
    m_craftAlchemyIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                        plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                        plasma::TEXTURE_WRAP_REPEAT));
    m_craftIngredientsIcon = loadTexture(directory + "inventory-ingredients.png", database);
    m_craftIngredientsIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                            plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                            plasma::TEXTURE_WRAP_REPEAT));
    plasma::Texture *equipmentIcon = loadTexture(directory + "inventory-equipment.png", database);
    equipmentIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                   plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                   plasma::TEXTURE_WRAP_REPEAT));
    plasma::Texture *ingredientsIcon = loadTexture(directory + "inventory-ingredients.png", database);
    ingredientsIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                                     plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                                     plasma::TEXTURE_WRAP_REPEAT));
    plasma::Texture *itemsIcon = loadTexture(directory + "inventory-items.png", database);
    itemsIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                               plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                               plasma::TEXTURE_WRAP_REPEAT));
    plasma::Texture *petsIcon = loadTexture(directory + "inventory-pets.png", database);
    petsIcon->setFormat(plasma::TextureFormat(plasma::TEXTURE_RGBA, plasma::TEXTURE_FILTER_NEAREST,
                                              plasma::TEXTURE_FILTER_NEAREST, plasma::TEXTURE_WRAP_REPEAT,
                                              plasma::TEXTURE_WRAP_REPEAT));

    // The start menu.
    m_startMenuNode = engine->createNode(0, 0, 0, m_startScreen);
    m_startMenuWidget = new StartMenuWidget(m_engine, m_startMenuNode, this);
    m_startMenuWidget->resizeTo(200.0f, 100.0f, true);

    // The panels: each an instance of the black widget template with its own transformation and
    // display, holding a widget on a node of its own (moved into the panel's widget by the template's
    // Name).
    if (m_blackWidget) {
        // The character sheet.
        plasma::Node *characterPanel = m_blackWidget->instance(m_gameScreen);
        characterPanel->setTransformation(characterPanel->transformation()->clone());
        characterPanel->setDisplay(characterPanel->display()->clone());
        characterPanel->widget()->resizeTo(260.0f, 200.0f, true);
        plasma::Node *characterNode = engine->createNode(0, 0, 0, characterPanel);
        m_characterWidget = new CharacterWidget(m_engine, characterNode, this);
        m_characterWidget->moveTo(0.0f, 0.0f, true);

        // The trade window (a shop's stock and the player's sold items), with the buy and buy-back tabs.
        plasma::Node *tab = m_gameScreen->findNode(L"tab");
        std::list<plasma::Node *> tabs;
        m_tradePanel = m_blackWidget->instance(m_gameScreen);
        m_tradePanel->setTransformation(m_tradePanel->transformation()->clone());
        m_tradePanel->setDisplay(m_tradePanel->display()->clone());
        m_tradePanel->setVisible(false);
        m_tradePanel->widget()->resizeTo(350.0f, 568.0f, true);
        plasma::Node *tradeNode = engine->createNode(0, 0, 0, 0);
        m_tradeWidget = new InventoryWidget(this, InventoryViewVendor, tradeNode, m_wideItemBox, upButton, downButton,
                                            scrollButton, 0);
        m_tradeWidget->m_inventory = &m_tradeInventory;
        m_tradePanel->moveIntoWidget(m_blackWidget->getName(), m_tradeWidget, true);
        tabs.clear();
        plasma::Node *tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = buyIcon->id();
        smoothMeshShape(tabNode->getShape())->updateDrawingTextures();
        tabs.push_back(tabNode);
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = buyBackIcon->id();
        smoothMeshShape(tabNode->getShape())->updateDrawingTextures();
        tabs.push_back(tabNode);
        m_tradeWidget->setTabNodes(tabs);

        // The crafting window (key C); its tabs depend on the class.
        m_craftingNode = m_blackWidget->instance(m_gameScreen);
        m_craftingNode->setTransformation(m_craftingNode->transformation()->clone());
        m_craftingNode->setDisplay(m_craftingNode->display()->clone());
        m_craftingNode->widget()->resizeTo(350.0f, 285.0f, true);
        m_craftingNode->widget()->moveTo(500.0f, 300.0f, true);
        m_craftingNode->setVisible(false);
        plasma::Node *craftingNode = engine->createNode(0, 0, 0, 0);
        m_craftingWidget = new InventoryWidget(this, InventoryViewCrafting, craftingNode, m_wideItemBox, upButton,
                                               downButton, scrollButton, m_itemSelector);
        m_craftingWidget->m_inventory = &m_craftingInventory;
        m_craftingNode->moveIntoWidget(m_blackWidget->getName(), m_craftingWidget, true);
        updateStyle();

        // The blueprint of the selected recipe, with its craft button.
        m_blueprintNode = m_blackWidget->instance(m_gameScreen);
        m_blueprintNode->setTransformation(m_blueprintNode->transformation()->clone());
        m_blueprintNode->setDisplay(m_blueprintNode->display()->clone());
        m_blueprintNode->setVisible(false);
        m_blueprintNode->widget()->resizeTo(350.0f, 330.0f, true);
        m_blueprintNode->widget()->moveTo(920.0f, 300.0f, true);
        plasma::Node *blueprintNode = engine->createNode(0, 0, 0, 0);
        plasma::Node *craftButton = m_gameScreen->findNode(L"craftbutton");
        smoothMeshShape(craftButton->getShape())->m_texture = m_abilityIcons[AbilityTwoHandedSweep]->id();
        smoothMeshShape(craftButton->getShape())->updateDrawingTextures();
        plasma::Node *craftBar = m_gameScreen->findNode(L"craftbar");
        m_blueprintPreview = new BlueprintPreviewWidget(m_engine, blueprintNode, this,
                                                        m_gameScreen->findNode(L"itemshadow"), craftButton, craftBar);
        m_blueprintNode->moveIntoWidget(m_blackWidget->getName(), m_blueprintPreview, true);
        tab->setParent(0);

        // The character creation's style selection.
        m_characterStylePanel = m_blackWidget->instance(m_characterCreationScreen);
        m_characterStylePanel->setTransformation(m_characterStylePanel->transformation()->clone());
        m_characterStylePanel->setDisplay(m_characterStylePanel->display()->clone());
        m_characterStylePanel->widget()->resizeTo(300.0f, 330.0f, true);
        plasma::Node *styleNode = engine->createNode(0, 0, 0, 0);
        m_characterStyleWidget = new CharacterStyleWidget(m_engine, styleNode, this, leftButton, rightButton);
        m_characterStylePanel->moveIntoWidget(m_blackWidget->getName(), m_characterStyleWidget, true);

        // The options, with the display's resolutions.
        m_optionsPanel = m_blackWidget->instance(m_gameScreen);
        m_optionsPanel->setTransformation(m_optionsPanel->transformation()->clone());
        m_optionsPanel->setDisplay(m_optionsPanel->display()->clone());
        m_optionsPanel->widget()->resizeTo(450.0f, 440.0f, true);
        m_optionsPanel->setVisible(false);
        plasma::Node *optionsNode = engine->createNode(0, 0, 0, 0);
        m_optionsWidget =
            new OptionsWidget(m_engine, optionsNode, this, leftButton, rightButton, secondaryButtonTemplate);
        m_optionsWidget->m_resolutions = *resolutions;
        m_optionsWidget->setOptions(*options);
        m_optionsPanel->moveIntoWidget(m_blackWidget->getName(), m_optionsWidget, true);

        // The system menu (key O).
        m_systemPanel = m_blackWidget->instance(m_gameScreen);
        m_systemPanel->setTransformation(m_systemPanel->transformation()->clone());
        m_systemPanel->setDisplay(m_systemPanel->display()->clone());
        m_systemPanel->widget()->resizeTo(150.0f, 120.0f, true);
        m_systemPanel->setVisible(false);
        plasma::Node *systemNode = engine->createNode(0, 0, 0, 0);
        m_systemWidget = new SystemWidget(m_engine, systemNode, this);
        m_systemPanel->moveIntoWidget(m_blackWidget->getName(), m_systemWidget, true);

        // The identifier's panel.
        m_enchantPanel = m_blackWidget->instance(m_gameScreen);
        m_enchantPanel->setTransformation(m_enchantPanel->transformation()->clone());
        m_enchantPanel->setDisplay(m_enchantPanel->display()->clone());
        m_enchantPanel->widget()->resizeTo(350.0f, 285.0f, true);
        m_enchantPanel->setVisible(false);
        plasma::Node *enchantNode = engine->createNode(0, 0, 0, 0);
        plasma::Node *itemFrame = m_gameScreen->findNode(L"itemframe");
        m_enchantWidget = new EnchantWidget(m_engine, enchantNode, this, itemFrame);
        m_enchantPanel->moveIntoWidget(m_blackWidget->getName(), m_enchantWidget, true);

        // The adapter's panel.
        m_adaptionPanel = m_blackWidget->instance(m_gameScreen);
        m_adaptionPanel->setTransformation(m_adaptionPanel->transformation()->clone());
        m_adaptionPanel->setDisplay(m_adaptionPanel->display()->clone());
        m_adaptionPanel->widget()->resizeTo(350.0f, 400.0f, true);
        m_adaptionPanel->setVisible(false);
        plasma::Node *adaptionNode = engine->createNode(0, 0, 0, 0);
        plasma::Node *rightArrow = m_gameScreen->findNode(L"rightarrow");
        m_adaptionWidget = new AdaptionWidget(m_engine, adaptionNode, this, itemFrame, rightArrow);
        m_adaptionPanel->moveIntoWidget(m_blackWidget->getName(), m_adaptionWidget, true);

        // The skills (key X).
        m_skillPanel = m_blackWidget->instance(m_gameScreen);
        m_skillPanel->setTransformation(m_skillPanel->transformation()->clone());
        m_skillPanel->setDisplay(m_skillPanel->display()->clone());
        m_skillPanel->widget()->resizeTo(300.0f, 460.0f, true);
        plasma::Node *skillNode = engine->createNode(0, 0, 0, 0);
        m_skillWidget = new SkillWidget(m_engine, skillNode, this);
        m_skillPanel->moveIntoWidget(m_blackWidget->getName(), m_skillWidget, true);
        itemFrame->setParent(0);
        rightArrow->setParent(0);

        // The saved characters ("num" counts the slots).
        m_charactersDatabase.open("Save/characters.db");
        BytesIO characterCount;
        int characters = 0;
        if (m_charactersDatabase.read("num", characterCount)) {
            characterCount.read(&characters, sizeof(characters));
            for (int i = 0; i < characters; i++) {
                Creature *character = new Creature(0);
                loadCharacter(i, character);
                m_characters.push_back(character);
            }
        }
        // The saved worlds.
        m_worldsDatabase.open("Save/worlds.db");
        BytesIO worldCount;
        int worlds = 0;
        if (m_worldsDatabase.read("num", worldCount)) {
            worldCount.read(&worlds, sizeof(worlds));
            for (int i = 0; i < worlds; i++) {
                WorldInfo *world = new WorldInfo;
                loadWorldInfo(i, world);
                m_worlds.push_back(world);
            }
        }

        // The hovered item's voxel preview.
        m_voxelPanel = m_blackWidget->instance(m_gameScreen);
        m_voxelPanel->setTransformation(m_voxelPanel->transformation()->clone());
        m_voxelPanel->setDisplay(m_voxelPanel->display()->clone());
        m_voxelPanel->setVisible(false);
        m_voxelPanel->widget()->resizeTo(400.0f, 624.0f, true);
        plasma::Node *voxelNode = engine->createNode(0, 0, 0, 0);
        m_voxelWidget = new VoxelWidget(m_engine, voxelNode, this);
        m_voxelPanel->moveIntoWidget(m_blackWidget->getName(), m_voxelWidget, true);

        // The inventory (keys B and I) with its four tabs.
        m_inventoryNode = m_blackWidget->instance(m_gameScreen);
        m_inventoryNode->setTransformation(m_inventoryNode->transformation()->clone());
        m_inventoryNode->setDisplay(m_inventoryNode->display()->clone());
        m_inventoryNode->setVisible(false);
        m_inventoryNode->widget()->resizeTo(400.0f, 285.0f, true);
        plasma::Node *inventoryNode = engine->createNode(0, 0, 0, 0);
        m_inventoryWidget = new InventoryWidget(this, InventoryViewBag, inventoryNode, m_itemBox, upButton, downButton,
                                                scrollButton, 0);
        m_inventoryNode->moveIntoWidget(m_blackWidget->getName(), m_inventoryWidget, true);
        tabs.clear();
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = equipmentIcon->id();
        smoothMeshShape(tabNode->getShape())->updateDrawingTextures();
        tabs.push_back(tabNode);
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = itemsIcon->id();
        smoothMeshShape(tabNode->getShape())->updateDrawingTextures();
        tabs.push_back(tabNode);
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = ingredientsIcon->id();
        smoothMeshShape(tabNode->getShape())->updateDrawingTextures();
        tabs.push_back(tabNode);
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = petsIcon->id();
        smoothMeshShape(tabNode->getShape())->updateDrawingTextures();
        tabs.push_back(tabNode);
        m_inventoryWidget->setTabNodes(tabs);
    }

    // The skill screen: the two specializations and eleven skills (the even ones of the first seven
    // are main skills).
    m_specializationButtons[SpecializationFirst] = specializationButton->clone(m_gameScreen);
    m_specializationButtons[SpecializationSecond] = specializationButton->clone(m_gameScreen);
    m_engine->deleteNode(specializationButton);
    for (int i = 0; i < SkillCount; i++) {
        if (i == SkillClassAbilityFirst || i == SkillPetTaming || i == SkillClimbing || i == SkillSwimming)
            m_skillButtons.push_back(skillButton->clone(m_gameScreen));
        else
            m_skillButtons.push_back(childSkillButton->clone(m_gameScreen));
    }
    m_engine->deleteNode(skillButton);
    m_engine->deleteNode(childSkillButton);

    // The character list's buttons.
    m_selectCharacterButton = secondaryButtonTemplate->clone(m_characterListScreen);
    m_selectCharacterButton->widget()->resizeTo(200.0f, 30.0f, true);
    m_selectCharacterButton->setText(L"Select");
    m_selectCharacterButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this,
                                               &GameController::onSelectCharacter, true, true);
    m_characterMenuButton = smallButton->clone(m_characterListScreen);
    m_characterMenuButton->widget()->resizeTo(20.0f, 20.0f, true);
    m_characterMenuButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this,
                                             &GameController::onEditCharacter, true, true);
    m_characterMenuButton->setVisible(false);
    m_deleteCharacterButton = button->clone(m_characterListScreen);
    m_deleteCharacterButton->widget()->resizeTo(300.0f, 30.0f, true);
    m_deleteCharacterButton->setText(L"Delete Character");
    m_deleteCharacterButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this,
                                               &GameController::onDeleteCharacter, true, true);
    m_deleteCharacterButton->setVisible(false);
    refreshCharacterList();

    // The world list's buttons.
    m_selectWorldButton = secondaryButtonTemplate->clone(m_worldListScreen);
    m_selectWorldButton->widget()->resizeTo(200.0f, 30.0f, true);
    m_selectWorldButton->setText(L"Select");
    m_selectWorldButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this, &GameController::onSelectWorld,
                                           true, true);
    m_worldMenuButton = smallButton->clone(m_worldListScreen);
    m_worldMenuButton->widget()->resizeTo(20.0f, 20.0f, true);
    m_worldMenuButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this, &GameController::onEditWorld,
                                         true, true);
    m_worldMenuButton->setVisible(false);
    m_deleteWorldButton = button->clone(m_worldListScreen);
    m_deleteWorldButton->widget()->resizeTo(300.0f, 30.0f, true);
    m_deleteWorldButton->setText(L"Delete World");
    m_deleteWorldButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this, &GameController::onDeleteWorld,
                                           true, true);
    m_deleteWorldButton->setVisible(false);
    updateWorldList();

    // The chat.
    m_chatNode = m_engine->createNode(0, 0, 0, m_gameScreen);
    m_chatWidget = new ChatWidget(m_engine, m_chatNode);
    m_chatWidget->resizeTo(400.0f, 200.0f, true);

    // The item previews are drawn over the other children of their parents.
    m_equippedPreviewNode->setParent(m_equippedPreviewNode->getParent());
    m_previewNode->setParent(m_previewNode->getParent());
    m_engine->initializeWidgets();
    m_engine->root()->initializeWidgets();
    delete database;
}

Chunk::~Chunk()
{
    for (std::list<ChunkBuffer *>::iterator it = chunkBuffers.begin(); it != chunkBuffers.end(); ++it)
        delete *it;
    for (std::list<ChunkBuffer *>::iterator it = newChunkBuffers.begin(); it != newChunkBuffers.end(); ++it)
        delete *it;
    DeleteCriticalSection(&lock);
}

GameController::~GameController()
{
    delete m_localServer;
    m_world.enterRegions();
    // Keep the character played in the local world, and save it and the world.
    if (!m_world.getName().empty() && m_selectedCharacter >= 0 && m_selectedCharacter < (int)m_characters.size()) {
        m_characters[m_selectedCharacter]->m_entityData = m_player->m_entityData;
        *m_characters[m_selectedCharacter]->m_playerData = *m_player->m_playerData;
        m_characters[m_selectedCharacter]->m_inventory = m_player->m_inventory;
        m_characters[m_selectedCharacter]->m_ridingStamina = m_player->m_ridingStamina;
        m_characters[m_selectedCharacter]->unused119c = m_player->unused119c;
    }
    if (m_selectedCharacter >= 0 && m_selectedCharacter < (int)m_characters.size() && !m_world.getName().empty())
        saveCharacter(m_selectedCharacter, m_characters[m_selectedCharacter]);
    if (!m_world.getName().empty() && m_selectedWorld >= 0 && m_selectedWorld < (int)m_worlds.size() &&
        m_worlds[m_selectedWorld]->m_seed == m_world.getSeed() &&
        m_worlds[m_selectedWorld]->m_name == m_world.getName())
        saveWorldInfo(m_selectedWorld, m_worlds[m_selectedWorld], true);
    m_world.leaveRegions();

    if (m_shadowTexture)
        m_shadowTexture->Release();
    if (m_sunTexture)
        m_sunTexture->Release();

    // Stop the threads and close the connection.
    m_running = false;
    m_connected = false;
    WaitForSingleObject(m_chunkThread, INFINITE);
    WaitForSingleObject(m_mapThread, INFINITE);
    if (m_receiveThread)
        WaitForSingleObject(m_receiveThread, INFINITE);
    if (m_sendThread)
        WaitForSingleObject(m_sendThread, INFINITE);
    if (m_socket)
        closesocket(m_socket);
    m_zoneGenerationRunning = false;
    WaitForSingleObject(m_zoneGenerationThread, INFINITE);

    delete[] m_chunks;
    delete m_cubeSprite;
    delete m_buildCursorSprite;
    for (std::vector<Creature *>::iterator it = m_characters.begin(); it != m_characters.end(); ++it)
        delete *it;
    for (std::vector<WorldInfo *>::iterator it = m_worlds.begin(); it != m_worlds.end(); ++it)
        delete *it;
    DeleteCriticalSection(&m_charactersLock);
    DeleteCriticalSection(&m_streamingLock);
    DeleteCriticalSection(&m_chunkUpdateLock);
    DeleteCriticalSection(&m_mapUpdateLock);
    DeleteCriticalSection(&m_updateLock2);
}

void GameController::applyOptions(const Options &options)
{
    m_world.enterRegions();
    m_options = options;
    m_options.save();
    m_audio->setMusicVolume(options.musicVolume * 0.01f, 0.0f);
    m_world.leaveRegions();
}

void GameController::beginUpdate()
{
    EnterCriticalSection(&m_chunkUpdateLock);
    EnterCriticalSection(&m_mapUpdateLock);
    EnterCriticalSection(&m_updateLock2);
}

bool GameController::canMesh(int x, int y)
{
    if (x < 0 || y < 0 || x * BlocksPerChunk >= BlocksPerWorld || y * BlocksPerChunk >= BlocksPerWorld)
        return false;
    int zoneX = x * BlocksPerChunk / BlocksPerZone;
    int zoneY = y * BlocksPerChunk / BlocksPerZone;
    if (!m_world.zone(zoneX, zoneY))
        return false;
    int neighborX = 0;
    int neighborY = 0;
    if (x * BlocksPerChunk % BlocksPerZone == 0)
        neighborX = -1;
    if (y * BlocksPerChunk % BlocksPerZone == 0)
        neighborY = -1;
    if (x * BlocksPerChunk % BlocksPerZone == BlocksPerZone - BlocksPerChunk)
        neighborX = 1;
    if (y * BlocksPerChunk % BlocksPerZone == BlocksPerZone - BlocksPerChunk)
        neighborY = 1;
    if (neighborX || neighborY) {
        x += neighborX;
        y += neighborY;
        if (!m_world.zone(x * 32 / BlocksPerZone, y * 32 / BlocksPerZone))
            return false;
        if (neighborX && neighborY) {
            if (!m_world.zone(x * 32 / BlocksPerZone, zoneY))
                return false;
            if (!m_world.zone(zoneX, y * 32 / BlocksPerZone))
                return false;
        }
    }
    return true;
}

// Switches the local game to another world: the player remembers it, and every creature but the
// player leaves the world (under both locks).
void GameController::changeWorld(int seed, const std::string &name)
{
    m_world.enterRegions();
    EnterCriticalSection(&m_streamingLock);
    m_player->m_playerData->lastWorld = std::make_pair(seed, name);
    m_seed = seed;
    m_worldName = name;
    std::list<std::map<long long, Creature *>::iterator> others;
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         ++it) {
        if (it->second != m_player)
            others.push_back(it);
    }
    for (std::list<std::map<long long, Creature *>::iterator>::iterator it = others.begin(); it != others.end(); ++it) {
        delete (*it)->second;
        m_world.m_creatures.erase(*it);
    }
    LeaveCriticalSection(&m_streamingLock);
    m_world.leaveRegions();
}

Creature *GameController::closestPet()
{
    Creature *nearest = 0;
    float nearestDistance = PetSearchRadiusSquared;
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         ++it) {
        Creature *creature = it->second;
        if (creature && creature->m_entityData.hostilityType == HostilityPet) {
            plasma::Vector<3, float> offset(m_player->m_entityData.position - creature->m_entityData.position);
            float distance = offset[1] * offset[1] + offset[0] * offset[0] + offset[2] * offset[2];
            if (distance < nearestDistance) {
                nearest = creature;
                nearestDistance = distance;
            }
        }
    }
    return nearest;
}

// Connects to a server (a dotted address or a host name) on port 12345: sends PacketHello with the
// protocol version and reads the answer. On PacketWelcome the server sends the player's creature
// id and entity data; the received entity data is not used: the local character's entity data,
// inventory and player data move to a creature with that id, replacing every other creature, and
// every loaded region forgets its missions. Then the receiving and sending threads start. Errors
// are shown on the connect screen.
void GameController::connect(const std::string &address)
// The chat's messages are coloured RGBA (0..1): orange for the connection's state.
{
    if (address.empty())
        return;
    // The world is the server's now: it is neither saved nor generated here.
    m_world.m_client = true;
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    unsigned long host = inet_addr(address.c_str());
    if (host == INADDR_NONE) {
        hostent *entry = gethostbyname(address.c_str());
        if (entry)
            host = *(unsigned long *)entry->h_addr_list[0];
    }
    sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = host;
    server.sin_port = htons(ServerPort);
    if (::connect(m_socket, (sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        std::wstringstream text;
        text << L"Error: could not connect to server." << std::endl;
        m_connectionErrorNode->setText(text.str());
        closesocket(m_socket);
        m_socket = 0;
        m_world.m_client = false;
        m_connectScreen->setVisible(true);
        return;
    }

    int packetId = PacketHello;
    if (send(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR) {
        m_connectionErrorNode->setText(L"Connection error.");
        closesocket(m_socket);
        m_socket = 0;
        m_world.m_client = false;
        m_connectScreen->setVisible(true);
        return;
    }
    int version = ProtocolVersion;
    if (send(m_socket, (char *)&version, sizeof(version), 0) == SOCKET_ERROR) {
        m_connectionErrorNode->setText(L"Connection error.");
        closesocket(m_socket);
        m_socket = 0;
        m_world.m_client = false;
        m_connectScreen->setVisible(true);
        return;
    }
    if (receiveAll(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR) {
        m_connectionErrorNode->setText(L"Connection error.");
        closesocket(m_socket);
        m_socket = 0;
        m_world.m_client = false;
        m_connectScreen->setVisible(true);
        return;
    }

    if (packetId == PacketWelcome) {
        m_world.enterRegions();
        m_chatWidget->printMessage(L"Connected.\n", plasma::Vector<4, float>(1.0f, 0.5f, 0.2f, 1.0f));
        m_world.leaveRegions();
        if (receiveAll(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR) {
            m_connectionErrorNode->setText(L"Connection error.");
            closesocket(m_socket);
            m_socket = 0;
            m_world.m_client = false;
            return;
        }
        if (packetId != PacketEntityData)
            return;
        long long id = 0;
        if (receiveAll(m_socket, (char *)&id, sizeof(id), 0) == SOCKET_ERROR)
            return;
        Creature::EntityData received;
        if (receiveAll(m_socket, (char *)&received, sizeof(received), 0) == SOCKET_ERROR)
            return;
        if (id >= 0) {
            m_world.enterRegions();
            Creature::EntityData entityData(m_player->m_entityData);
            Inventory inventory(m_player->m_inventory);
            Creature::PlayerData playerData(*m_player->m_playerData);
            float ridingStamina = m_player->m_ridingStamina;
            int unused119c = m_player->unused119c;
            for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin();
                 it != m_world.m_creatures.end(); ++it)
                delete it->second;
            m_world.m_creatures.clear();
            m_player = m_world.creatureByID(id);
            if (!m_player) {
                m_player = new Creature(id);
                m_world.m_creatures[id] = m_player;
            }
            m_player->m_entityData = entityData;
            m_player->m_inventory = inventory;
            if (!m_player->m_playerData)
                m_player->m_playerData = new Creature::PlayerData();
            *m_player->m_playerData = playerData;
            m_player->m_ridingStamina = ridingStamina;
            m_player->unused119c = unused119c;
            m_player->m_shownLevel = m_player->m_entityData.level;
            m_player->m_shownXp = m_player->m_entityData.xp;
            m_world.setLocalPlayer(m_player);
            // The server keeps the missions: clear them in every loaded region and mark the regions and their
            // zones.
            for (int x = 0; x < 0x400; x++) {
                for (int y = 0; y < 0x400; y++) {
                    Region *region = m_world.getRegion(x, y);
                    if (!region)
                        continue;
                    for (int i = 0; i < 8; i++) {
                        for (int j = 0; j < 8; j++) {
                            region->m_records[i * 8 + j].mission = MissionState();
                            region->m_records[i * 8 + j].monster = MonsterState();
                        }
                    }
                    region->m_needsServerUpdate = true;
                    for (int i = 0; i < 64; i++) {
                        for (int j = 0; j < 64; j++) {
                            Zone *zone = region->m_zones[i][j];
                            if (zone)
                                zone->m_needsServerUpdate = true;
                        }
                    }
                }
            }
            m_world.leaveRegions();
        }
        m_connected = true;
        m_receiveThread = StartThread([this]() {
            // The entity data of every creature before a full list arrives, and the ids of the creatures
            // updated since the last PacketEntitiesEnd.
            std::map<long long, Creature::EntityData> previous;
            bool disconnected = false;
            int lastTick = 0;
            std::set<long long> updated;
            timeBeginPeriod(1);
            while (m_connected && !disconnected) {
                int packetId = 0;
                if (receiveAll(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                    break;
                if (packetId == PacketEntitiesEnd) {
                    // The time between two server ticks.
                    static int tickTime;
                    int now = timeGetTime();
                    m_world.enterRegions();
                    if (lastTick)
                        tickTime = now - lastTick;
                    m_world.leaveRegions();
                    lastTick = now;
                }
                switch (packetId) {
                case PacketSeed: {
                    // The server's world: loaded under the name online_<seed> unless it is already.
                    int seed = 0;
                    if (receiveAll(m_socket, (char *)&seed, sizeof(seed), 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    std::stringstream name;
                    name << "online_" << seed;
                    std::string worldName = name.str();
                    if (seed != m_seed || m_worldName != worldName) {
                        changeWorld(seed, worldName);
                        m_gameScreen->setVisible(true);
                        m_characterCreationScreen->setVisible(false);
                        m_characterListScreen->setVisible(false);
                        m_worldListScreen->setVisible(false);
                    }
                    break;
                }
                case PacketEntityData: {
                    // One creature's entity data, compressed.
                    BytesIO data;
                    int size = 0;
                    if (receiveAll(m_socket, (char *)&size, sizeof(size), 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    if (size == 0) {
                        disconnected = true;
                        break;
                    }
                    data.m_data.resize(size);
                    if (receiveAll(m_socket, data.data(), size, 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    data.decompress();
                    long long id = 0;
                    data.read(&id, sizeof(id));
                    m_world.enterRegions();
                    Creature *creature = m_world.creatureByID(id);
                    bool created = false;
                    if (!creature) {
                        creature = new Creature(id);
                        m_world.m_creatures[id] = creature;
                        created = true;
                    }
                    Creature::EntityData &current = creature->m_entityData;
                    Creature::EntityData entityData(current);
                    m_world.leaveRegions();
                    if (readEntityData(data, entityData) == -1) {
                        disconnected = true;
                        break;
                    }
                    updated.insert(id);
                    m_world.enterRegions();
                    if (creature != m_player) {
                        if (!created && !(creature->m_entityData.origin != entityData.origin) &&
                            creature->m_entityData.hp > 0.0f) {
                            // Go on drawing it from where it is.
                            creature->m_smoothingTime = RemoteSmoothingTime;
                            creature->m_previousDrawnPosition = creature->m_drawnPosition;
                            creature->m_previousDrawnRotation = creature->m_drawnRotation;
                            creature->m_previousDrawnAim = creature->m_drawnAim;
                        } else {
                            creature->m_smoothingTime = 0;
                            creature->m_inventory.m_inventoryTabs.clear();
                            creature->m_drawnPosition = entityData.position;
                            creature->m_drawnRotation = entityData.rotation;
                            creature->m_previousDrawnPosition = creature->m_drawnPosition;
                            creature->m_previousDrawnRotation = creature->m_drawnRotation;
                        }
                        current = entityData;
                    }
                    m_world.leaveRegions();
                    break;
                }
                case PacketEntitiesEnd: {
                    // Remove the creatures the tick did not update.
                    std::list<std::map<long long, Creature *>::iterator> removed;
                    m_world.enterRegions();
                    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin();
                         it != m_world.m_creatures.end(); ++it) {
                        if (it->second && it->second != m_player && updated.find(it->first) == updated.end())
                            removed.push_back(it);
                    }
                    for (std::list<std::map<long long, Creature *>::iterator>::iterator it = removed.begin();
                         it != removed.end(); ++it) {
                        delete (*it)->second;
                        m_world.m_creatures.erase(*it);
                    }
                    m_world.leaveRegions();
                    updated.clear();
                    break;
                }
                case PacketObjects: {
                    // Every world object (airship) near the player; the others are removed.
                    int count;
                    if (receiveAll(m_socket, (char *)&count, sizeof(count), 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    if (count <= 0)
                        break;
                    std::vector<Zone::AirshipState> objects(count);
                    if (receiveAll(m_socket, (char *)&objects[0], count * sizeof(Zone::AirshipState), 0) ==
                        SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    m_world.enterRegions();
                    for (std::map<long long, Zone::Airship *>::iterator it = m_world.m_airships.begin();
                         it != m_world.m_airships.end(); ++it)
                        it->second->id = 0;
                    for (std::vector<Zone::AirshipState>::iterator it = objects.begin(); it != objects.end(); ++it) {
                        Zone::Airship *object = m_world.m_airships[it->id];
                        if (!object) {
                            object = new Zone::Airship();
                            m_world.m_airships[it->id] = object;
                        }
                        *(Zone::AirshipState *)object = *it;
                    }
                    std::list<std::map<long long, Zone::Airship *>::iterator> removed;
                    for (std::map<long long, Zone::Airship *>::iterator it = m_world.m_airships.begin();
                         it != m_world.m_airships.end(); ++it) {
                        if (it->second->id == 0)
                            removed.push_back(it);
                    }
                    for (std::list<std::map<long long, Zone::Airship *>::iterator>::iterator it = removed.begin();
                         it != removed.end(); ++it) {
                        delete (*it)->second;
                        m_world.m_airships.erase(*it);
                    }
                    m_world.leaveRegions();
                    break;
                }
                case PacketEntityList: {
                    // The entity data of every creature the player sees: the others are removed.
                    int count;
                    if (receiveAll(m_socket, (char *)&count, sizeof(count), 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    m_world.enterRegions();
                    previous.clear();
                    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin();
                         it != m_world.m_creatures.end(); ++it) {
                        if (it->second)
                            previous[it->second->m_id] = it->second->m_entityData;
                    }
                    m_world.leaveRegions();
                    std::set<long long> listed;
                    for (int i = 0; i < count; i++) {
                        long long id = 0;
                        if (receiveAll(m_socket, (char *)&id, sizeof(id), 0) == SOCKET_ERROR ||
                            receiveEntityData(m_socket, previous[id]) == SOCKET_ERROR) {
                            disconnected = true;
                            goto next;
                        }
                        listed.insert(id);
                    }
                    listed.insert(m_player->m_id);
                    {
                        std::list<std::map<long long, Creature *>::iterator> removed;
                        m_world.enterRegions();
                        for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin();
                             it != m_world.m_creatures.end(); ++it) {
                            if (it->second && listed.find(it->first) == listed.end())
                                removed.push_back(it);
                        }
                        for (std::list<std::map<long long, Creature *>::iterator>::iterator it = removed.begin();
                             it != removed.end(); ++it) {
                            delete (*it)->second;
                            m_world.m_creatures.erase(*it);
                        }
                        for (std::set<long long>::iterator it = listed.begin(); it != listed.end(); ++it) {
                            Creature *creature = m_world.creatureByID(*it);
                            bool created = false;
                            if (!creature) {
                                creature = new Creature(*it);
                                m_world.m_creatures[*it] = creature;
                                created = true;
                            }
                            if (creature == m_player)
                                continue;
                            Creature::EntityData &entityData = previous[*it];
                            if (!created && creature->m_entityData.origin == entityData.origin &&
                                creature->m_entityData.hp > 0.0f) {
                                creature->m_smoothingTime = RemoteSmoothingTime;
                                creature->m_previousDrawnPosition = creature->m_drawnPosition;
                                creature->m_previousDrawnRotation = creature->m_drawnRotation;
                                creature->m_previousDrawnAim = creature->m_drawnAim;
                            } else {
                                creature->m_smoothingTime = 0;
                                creature->m_inventory.m_inventoryTabs.clear();
                                creature->m_drawnPosition = entityData.position;
                                creature->m_drawnRotation = entityData.rotation;
                                creature->m_previousDrawnPosition = creature->m_drawnPosition;
                                creature->m_previousDrawnRotation = creature->m_drawnRotation;
                            }
                            creature->m_entityData = entityData;
                        }
                        m_world.leaveRegions();
                    }
                    break;
                }
                case PacketChat: {
                    ChatMessage message;
                    if (receiveAll(m_socket, (char *)&message.creatureId, sizeof(message.creatureId), 0) ==
                        SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    int length;
                    if (receiveAll(m_socket, (char *)&length, sizeof(length), 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    if (length <= 0)
                        break;
                    message.text.resize(length);
                    if (receiveAll(m_socket, (char *)&message.text[0], length * 2, 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    m_world.enterRegions();
                    m_chatMessages.push_back(message);
                    m_world.leaveRegions();
                    break;
                }
                case PacketTime: {
                    int day;
                    int time;
                    if (receiveAll(m_socket, (char *)&day, sizeof(day), 0) == SOCKET_ERROR ||
                        receiveAll(m_socket, (char *)&time, sizeof(time), 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    m_world.enterRegions();
                    m_world.m_day = day;
                    m_world.m_time = time;
                    m_world.leaveRegions();
                    break;
                }
                case PacketWorldUpdate: {
                    // The tick's events, compressed, added to the updates the game applies.
                    int size = 0;
                    if (receiveAll(m_socket, (char *)&size, sizeof(size), 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    BytesIO data;
                    data.m_data.resize(size);
                    if (receiveAll(m_socket, data.data(), size, 0) == SOCKET_ERROR) {
                        disconnected = true;
                        break;
                    }
                    data.decompress();
                    ServerUpdates updates;
                    readList(data, updates.blocks);
                    readList(data, updates.hits);
                    readList(data, updates.particles);
                    readList(data, updates.sounds);
                    readList(data, updates.projectiles);
                    readList(data, updates.objects);
                    int count = 0;
                    data.read(&count, sizeof(count));
                    for (int i = count; i > 0; i--) {
                        ServerUpdates::ItemListPacket packet;
                        data.read(&packet.zone, sizeof(packet.zone));
                        readList(data, packet.items);
                        updates.items.push_back(packet);
                    }
                    count = 0;
                    data.read(&count, sizeof(count));
                    for (int i = count; i > 0; i--) {
                        ServerUpdates::BuildingListPacket record;
                        data.read(&record.zone, sizeof(record.zone));
                        readList(data, record.buildings);
                        updates.buildings.push_back(record);
                    }
                    readList(data, updates.pickedUpItems);
                    readList(data, updates.kills);
                    readList(data, updates.aggro);
                    readList(data, updates.buffs);
                    readList(data, updates.missions);
                    m_world.enterRegions();
                    m_serverUpdates.blocks.insert(m_serverUpdates.blocks.end(), updates.blocks.begin(),
                                                  updates.blocks.end());
                    m_serverUpdates.hits.insert(m_serverUpdates.hits.end(), updates.hits.begin(), updates.hits.end());
                    m_serverUpdates.particles.insert(m_serverUpdates.particles.end(), updates.particles.begin(),
                                                     updates.particles.end());
                    m_serverUpdates.sounds.insert(m_serverUpdates.sounds.end(), updates.sounds.begin(),
                                                  updates.sounds.end());
                    m_serverUpdates.projectiles.insert(m_serverUpdates.projectiles.end(), updates.projectiles.begin(),
                                                       updates.projectiles.end());
                    m_serverUpdates.items.insert(m_serverUpdates.items.end(), updates.items.begin(),
                                                 updates.items.end());
                    m_serverUpdates.objects.insert(m_serverUpdates.objects.end(), updates.objects.begin(),
                                                   updates.objects.end());
                    m_serverUpdates.buildings.insert(m_serverUpdates.buildings.end(), updates.buildings.begin(),
                                                     updates.buildings.end());
                    m_serverUpdates.pickedUpItems.insert(m_serverUpdates.pickedUpItems.end(),
                                                         updates.pickedUpItems.begin(), updates.pickedUpItems.end());
                    m_serverUpdates.kills.insert(m_serverUpdates.kills.end(), updates.kills.begin(),
                                                 updates.kills.end());
                    m_serverUpdates.aggro.insert(m_serverUpdates.aggro.end(), updates.aggro.begin(),
                                                 updates.aggro.end());
                    m_serverUpdates.buffs.insert(m_serverUpdates.buffs.end(), updates.buffs.begin(),
                                                 updates.buffs.end());
                    m_serverUpdates.missions.insert(m_serverUpdates.missions.end(), updates.missions.begin(),
                                                    updates.missions.end());
                    m_world.leaveRegions();
                    break;
                }
                }
            next:;
            }
            timeEndPeriod(1);
        });
        SetThreadPriority(m_receiveThread, THREAD_PRIORITY_BELOW_NORMAL);
        m_sendThread = StartThread([this]() {
            // Every tick: the player's entity data (all of it at first, then what changed), the typed chat
            // line, the interaction and the queued hits, buffs and projectiles; every second the zones the map
            // needs and drops. At most one tick per 20 ms.
            bool full = true;
            Creature::EntityData sent;
            bool failed = false;
            timeBeginPeriod(1);
            int zoneTime = timeGetTime();
            while (m_connected && !failed) {
                int packetId = PacketEntityData;
                if (send(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                    break;
                BytesIO data;
                m_world.enterRegions();
                std::list<HitPacket> hits(m_hits);
                std::list<BuffPacket> buffs(m_buffs);
                std::list<BulletPacket> projectiles(m_projectiles);
                std::wstring chat;
                if (!m_outgoingChatQueue.empty()) {
                    chat = m_outgoingChatQueue.front();
                    m_outgoingChatQueue.pop_front();
                }
                bool interacting = !m_player->m_interactions.empty();
                Interaction interaction;
                if (interacting) {
                    interaction = m_player->m_interactions.front();
                    m_player->m_interactions.pop_front();
                }
                m_hits.clear();
                m_buffs.clear();
                m_projectiles.clear();
                long long id = m_player->m_id;
                Creature::EntityData entityData(m_player->m_entityData);
                m_world.leaveRegions();

                data.write(&id, sizeof(id));
                writeEntityData(data, sent, entityData, full);
                data.compress();
                int size = data.size();
                if (send(m_socket, (char *)&size, sizeof(size), 0) == SOCKET_ERROR)
                    break;
                if (send(m_socket, data.data(), size, 0) == SOCKET_ERROR)
                    break;
                full = false;
                sent = entityData;
                if (chat.size()) {
                    int length = (int)chat.size();
                    packetId = PacketChat;
                    if (send(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                        break;
                    if (send(m_socket, (char *)&length, sizeof(length), 0) == SOCKET_ERROR)
                        break;
                    if (send(m_socket, (char *)chat.c_str(), length * 2, 0) == SOCKET_ERROR)
                        break;
                }
                if (interacting) {
                    packetId = PacketInteraction;
                    if (send(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                        break;
                    if (send(m_socket, (char *)&interaction, sizeof(interaction), 0) == SOCKET_ERROR)
                        break;
                }
                for (std::list<HitPacket>::iterator it = hits.begin(); it != hits.end();) {
                    packetId = PacketHit;
                    if (send(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                        goto disconnected;
                    if (send(m_socket, (char *)&*it, sizeof(HitPacket), 0) == SOCKET_ERROR)
                        goto disconnected;
                    ++it;
                }
                for (std::list<BuffPacket>::iterator it = buffs.begin(); it != buffs.end(); ++it) {
                    packetId = PacketBuff;
                    if (send(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                        goto disconnected;
                    if (send(m_socket, (char *)&*it, sizeof(BuffPacket), 0) == SOCKET_ERROR)
                        goto disconnected;
                }
                for (std::list<BulletPacket>::iterator it = projectiles.begin(); it != projectiles.end(); ++it) {
                    packetId = PacketProjectile;
                    if (send(m_socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                        goto disconnected;
                    if (send(m_socket, (char *)&*it, sizeof(BulletPacket), 0) == SOCKET_ERROR)
                        goto disconnected;
                }
                {
                    EnterCriticalSection(&m_streamingLock);
                    std::list<plasma::Vector<2, int>> zoneRequests(m_zoneRequests);
                    std::list<plasma::Vector<2, int>> regionRequests(m_regionRequests);
                    LeaveCriticalSection(&m_streamingLock);
                    int now = timeGetTime();
                    if (now - zoneTime > 1000) {
                        zoneTime = now;
                        for (std::list<plasma::Vector<2, int>>::iterator it = zoneRequests.begin();
                             it != zoneRequests.end(); ++it) {
                            int requestId = PacketZoneRequest;
                            if (send(m_socket, (char *)&requestId, sizeof(requestId), 0) == SOCKET_ERROR) {
                                failed = true;
                                break;
                            }
                            if (send(m_socket, (char *)&*it, sizeof(plasma::Vector<2, int>), 0) == SOCKET_ERROR) {
                                failed = true;
                                break;
                            }
                        }
                        for (std::list<plasma::Vector<2, int>>::iterator it = regionRequests.begin();
                             it != regionRequests.end(); ++it) {
                            int requestId = PacketRegionRequest;
                            if (send(m_socket, (char *)&requestId, sizeof(requestId), 0) == SOCKET_ERROR) {
                                failed = true;
                                break;
                            }
                            if (send(m_socket, (char *)&*it, sizeof(plasma::Vector<2, int>), 0) == SOCKET_ERROR) {
                                failed = true;
                                break;
                            }
                        }
                    }
                    static int lastTick;
                    if (now - lastTick < 20)
                        Sleep(lastTick - now + 20);
                    lastTick = now;
                }
            }
        disconnected:
            if (m_connected) {
                closesocket(m_socket);
                m_socket = 0;
                m_world.m_client = false;
            }
            timeEndPeriod(1);
        });
        SetThreadPriority(m_sendThread, THREAD_PRIORITY_BELOW_NORMAL);
    } else if (packetId == PacketServerFull) {
        m_world.enterRegions();
        std::wstringstream text;
        text << L"Error: Server is full." << std::endl;
        m_connectionErrorNode->setText(text.str());
        m_world.leaveRegions();
        closesocket(m_socket);
        m_socket = 0;
        m_world.m_client = false;
        m_connectScreen->setVisible(true);
    } else if (packetId == PacketHello) {
        // The server's version (times 100) follows.
        int serverVersion = 0;
        if (receiveAll(m_socket, (char *)&serverVersion, sizeof(serverVersion), 0) == SOCKET_ERROR) {
            m_connectionErrorNode->setText(L"Connection error.");
            closesocket(m_socket);
            m_socket = 0;
            m_world.m_client = false;
            m_connectScreen->setVisible(true);
            return;
        }
        m_world.enterRegions();
        std::wstringstream text;
        text << L"Error: Server has different version (" << serverVersion * 0.01 << L") than client ("
             << ProtocolVersion * 0.01 << L")" << std::endl;
        m_connectionErrorNode->setText(text.str());
        m_world.leaveRegions();
        closesocket(m_socket);
        m_socket = 0;
        m_world.m_client = false;
        m_connectScreen->setVisible(true);
    } else {
        m_connectionErrorNode->setText(L"Connection error");
        // Leaves the world's lock without entering it first.
        m_world.leaveRegions();
        closesocket(m_socket);
        m_socket = 0;
        m_world.m_client = false;
        m_connectScreen->setVisible(true);
    }
}

// Makes the crafting formula's item: removes ingredients from the player's inventory, marks the result as crafted, and sometimes produces one to three extra units of a material.
void GameController::craft()
// The chat's warning is red (RGBA, 0..1).
{
    Item result(m_craftingFormula.result);
    if (!m_player->canTakeItem(result)) {
        m_chatWidget->printMessage(L"You can't carry more of these items.\n",
                                   plasma::Vector<4, float>(1.0f, 0.2f, 0.2f, 1.0f));
        playSound(SoundHumanVoice01, m_player->m_entityData.position, 1.0f, 1.0f);
        return;
    }
    for (std::vector<Ingredient>::iterator ingredient = m_craftingFormula.ingredients.begin();
         ingredient != m_craftingFormula.ingredients.end(); ++ingredient) {
        int needed = ingredient->quantity;
        for (int i = 0; i < (int)m_player->m_inventory.m_inventoryTabs.size(); i++) {
            for (std::vector<ItemStack>::iterator stack = m_player->m_inventory.m_inventoryTabs[i].begin();
                 stack != m_player->m_inventory.m_inventoryTabs[i].end(); ++stack) {
                if (stack->item == ingredient->item) {
                    if (needed < stack->quantity) {
                        stack->quantity -= needed;
                        break;
                    }
                    // The stack is emptied before its count is subtracted, so the subtraction has no effect.
                    stack->quantity = 0;
                    stack->item = Item();
                    needed -= stack->quantity;
                    if (needed == 0)
                        break;
                }
            }
        }
    }
    if (result.category == ItemCategoryWeapon || result.category == ItemCategoryChest ||
        result.category == ItemCategoryShoulder || result.category == ItemCategoryFeet ||
        result.category == ItemCategoryHands || result.category == ItemCategoryNeck ||
        result.category == ItemCategoryRing)
        result.flags |= (1 << ItemFlagAdapted);
    playSound(SoundCraft, m_cameraPosition, 1.0f, 1.0f);
    if (result.category == ItemCategoryMaterial && result.id != MaterialItemWaterFlask &&
        rand() % ExtraMaterialChance == 0) {
        int extra = 1;
        if (rand() % ExtraMaterialChance == 0) {
            extra = 2;
            if (rand() % ExtraMaterialChance == 0)
                extra = 3;
        }
        result.level += extra;
        playSound(SoundCraftProc, m_cameraPosition, 1.0f, 1.0f);
    }
    m_player->m_inventory.add(result);
    m_inventoryWidget->updateRows();
    m_inventoryWidget->updateScrollButton();
    notifyItem(result, m_player);
    updateFormulas();
}

// Breaks a sprite, placed by matrix, into particles of the given type (the release's
// Game::createParticles; render calls it for creatures). Every visible voxel with an empty
// neighbour becomes a particle with even odds, flying away from the sprite's centre with a random
// spin, tinted by color. The velocity, the factor and the origin passed in are not used: the
// particles are placed relative to m_drawOriginX/m_drawOriginY instead.
void GameController::createParticles(Sprite *sprite, const plasma::Matrix<float> &matrix,
                                     const plasma::Vector<3, float> &unusedVector,
                                     const plasma::Vector<3, float> &velocity, float unusedFactor, float originX,
                                     float originY, const plasma::Vector<4, float> &color, int particleType)
{
    if (!sprite)
        return;
    plasma::Vector<3, float> center =
        matrix *
        (plasma::Vector<3, float>((float)sprite->sizeX(), (float)sprite->sizeY(), (float)sprite->sizeZ()) * 0.5f);
    for (int x = 0; x < sprite->sizeX(); ++x) {
        for (int y = 0; y < sprite->sizeY(); ++y) {
            for (int z = 0; z < sprite->sizeZ(); ++z) {
                plasma::Vector<3, unsigned char> &voxel = sprite->voxel(x, y, z);
                if (sprite->isEmpty(&voxel, false))
                    continue;
                if (!sprite->isEmpty(&sprite->voxel(x - 1, y, z), false) &&
                    !sprite->isEmpty(&sprite->voxel(x + 1, y, z), false) &&
                    !sprite->isEmpty(&sprite->voxel(x, y - 1, z), false) &&
                    !sprite->isEmpty(&sprite->voxel(x, y + 1, z), false) &&
                    !sprite->isEmpty(&sprite->voxel(x, y, z - 1), false) &&
                    !sprite->isEmpty(&sprite->voxel(x, y, z + 1), false))
                    continue;
                if (rand() % 2 != 0)
                    continue;
                Particle particle;
                particle.type = particleType;
                particle.age = -(rand() % VoxelParticleDelay);
                particle.position = matrix * plasma::Vector<3, float>((float)x, (float)y, (float)z);
                plasma::Vector<3, Fixed> centerPosition(center);
                particle.velocity = (particle.position - centerPosition) * Fixed(2.0f);
                particle.rotation = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                float spinX = (float)rand() / float(RAND_MAX) - 0.5f;
                float spinY = (float)rand() / float(RAND_MAX) - 0.5f;
                float spinZ = (float)rand() / float(RAND_MAX) - 0.5f;
                particle.spin = plasma::Vector<3, float>(spinX, spinY, spinZ) * 100.0f;
                particle.color =
                    plasma::Vector<4, float>(voxel[0] / 255.0f, voxel[1] / 255.0f, voxel[2] / 255.0f, 1.0f) * color;
                particle.size = 1.0f;
                particle.matrix = matrix;
                particle.matrix[12] = 0.0f;
                particle.matrix[13] = 0.0f;
                particle.matrix[14] = 0.0f;
                particle.collidesWithBlocks = true;
                particle.lifetime = VoxelParticleLifetime;
                particle.position[0] -= m_drawOriginX;
                particle.position[1] -= m_drawOriginY;
                particle.startPosition = particle.position;
                // Insert at the front, before the first element.
                m_particles.push_front(particle);
            }
        }
    }
}

void GameController::disconnect()
// The chat's message is orange (RGBA, 0..1).
{
    if (m_connected) {
        saveCharacter(m_selectedCharacter, m_player);
        m_connected = false;
        if (m_socket) {
            closesocket(m_socket);
            m_socket = 0;
            m_world.m_client = false;
        }
        if (m_receiveThread) {
            WaitForSingleObject(m_receiveThread, INFINITE);
            m_receiveThread = 0;
        }
        if (m_sendThread) {
            WaitForSingleObject(m_sendThread, INFINITE);
            m_sendThread = 0;
        }
        m_world.enterRegions();
        m_world.m_client = false;
        m_chatWidget->printMessage(L"Disconnected.", plasma::Vector<4, float>(1.0f, 0.5f, 0.2f, 1.0f));
        m_world.leaveRegions();
        changeWorld(m_world.m_seed, m_world.m_name);
    }
}

// Draw each spirit as a unit cube at its voxel in the item's model, colored by material.
void GameController::drawCubes(const Item *item, const plasma::Matrix<float> &model, const plasma::Matrix<float> &view,
                               const plasma::Matrix<float> &projection, const plasma::Vector<4, float> &color,
                               float glow)
{
    if (item->category != ItemCategoryNone) {
        m_shader->setShininess(item->shininess());
        for (int i = 0; i < item->spiritCount; i++) {
            plasma::Matrix<float> matrix = model;
            matrix.translate(plasma::Vector<3, float>(item->spirits[i].position));
            plasma::Vector<4, float> cubeColor = materialColor(item->spirits[i].material, color, glow);
            m_shader->setMaterialColor(cubeColor);
            m_shader->setMatrices(matrix, view, projection);
            m_cubeSprite->draw();
        }
        m_shader->setShininess(0.0f);
    }
}

// Three cubes per step along vector from position, circling it in the plane across it, each at its
// own speed and in its own colour (inner, middle, outer), turning with the time. A clipped impact
// stops at the first solid block within 20 steps of the vector and sheds an occasional particle in
// the inner colour. The effect's shape is hand-set: the cubes start a tenth of a step ahead, the
// three helices turn at 0.005, 0.004 and 0.006 half-turns per millisecond with a second circle of
// 0.3 times the angle (half the radius for the middle and outer ones), each a hundredth wider than
// the one inside, the cubes 0.1 to 0.3 of the radius in size; a shed particle rises 5 to 10 blocks
// per second, drifts up to 5 sideways and is 0.05 to 0.055 of the scale in size.
void GameController::drawImpact(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &vector,
                                const plasma::Vector<3, float> &rotation, const plasma::Vector<4, float> &innerColor,
                                const plasma::Vector<4, float> &middleColor, const plasma::Vector<4, float> &outerColor,
                                int time, const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection,
                                float width, float scale, float frequency, int count, bool clipped)
{
    plasma::Vector<3, float> direction = vector;
    float lengthSquared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
    // Two vectors across the direction, as long as it: the cubes circle in their plane.
    plasma::Vector<3, float> side = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f).cross(direction);
    if (lengthSquared < 0.00001f)
        side = plasma::Vector<3, float>(0.0f, 1.0f, 0.0f).cross(direction);
    side = side.normalized();
    plasma::Vector<3, float> up = side.cross(direction).normalized();
    float length = sqrt(lengthSquared);
    side = side * length;
    up = up * length;

    float distance = 0.0f;
    plasma::Vector<3, Fixed> point = position;
    plasma::Vector<3, float> step = direction * (1.0f / direction.length());
    if (clipped) {
        for (int i = 0; i < 20; i++) {
            if (m_world.getBlock(point[0], point[1], point[2], 0).isSolid())
                break;
            point = point + plasma::Vector<3, Fixed>(step);
            distance += 1.0f;
        }
        direction = vector * distance;
    }

    for (int i = 0; i < count; i++) {
        float progress = ((float)i + 1.0f + (float)(time % count) / (float)count) / (float)count;
        plasma::Vector<3, float> center = plasma::Vector<3, float>(
            plasma::Vector<3, Fixed>(position[0] + m_drawOriginX, position[1] + m_drawOriginY, position[2]) +
            plasma::Vector<3, Fixed>(direction * progress));
        // Now and then a particle where the step passes a multiple of 200 clock units.
        if (clipped && rand() % ImpactParticleChance == 0 &&
            (m_world.getClock() + m_frameMilliseconds + i * 200 / count) / 200 !=
                (m_world.getClock() + i * 200 / count) / 200) {
            Particle particle;
            particle.type = ParticleGlow;
            particle.position = position + plasma::Vector<3, Fixed>(direction * progress);
            // The draws go to z, y, then x.
            float rise = (float)rand() * 5.0f / float(RAND_MAX) + 5.0f;
            float driftY = 5.0f - (float)rand() * 10.0f / float(RAND_MAX);
            float driftX = 5.0f - (float)rand() * 10.0f / float(RAND_MAX);
            particle.velocity = plasma::Vector<3, float>(driftX, driftY, rise);
            particle.color = innerColor;
            particle.size = ((float)rand() * 0.005f / float(RAND_MAX) + 0.05f) * scale;
            m_particles.push_back(particle);
        }

        progress += 0.1f;
        float radius = progress * width;
        double phase = progress * 3.141592653589793;

        float angle = (float)((phase * 2.0 * length - time * 0.005f * 3.141592653589793) * frequency);
        plasma::Vector<3, float> offset = center + side * radius * cosf(angle) + up * radius * sinf(angle);
        offset = offset + (side * radius * cosf(angle * -0.3f) + up * radius * sinf(angle * -0.3f));
        plasma::Matrix<float> model;
        model.identity();
        model.translate(offset);
        model.rotate(rotation[2], plasma::Vector<3, float>(0.0f, 0.0f, 1.0f));
        progress *= length;
        float spin = progress * 360.0f;
        model.rotateX(spin);
        model.rotateY(spin);
        model.rotateZ(progress * -360.0f);
        float size = (cosf(angle * 0.9f) * 0.1f + 0.2f) * (radius + 0.2f) * scale;
        model.scale(size, size, size);
        model.translate(plasma::Vector<3, float>(-0.5f, -0.5f, -0.5f));
        m_shader->setMatrices(model, view, projection);
        m_shader->setMaterialColor(innerColor);
        m_cubeSprite->draw();

        radius *= 1.01f;
        angle = (float)((phase * 3.0 * length - time * 0.004f * 3.141592653589793) * frequency);
        offset = center + side * radius * cosf(angle) + up * radius * sinf(angle);
        offset = side * radius * 0.5f * cosf(angle * 0.3f) + up * radius * 0.5f * sinf(angle * 0.3f) + offset;
        model.identity();
        model.translate(offset);
        model.rotate(rotation[2], plasma::Vector<3, float>(0.0f, 0.0f, 1.0f));
        model.rotateX(progress * 360.0f * 2.0f);
        model.rotateY(progress * 360.0f * 2.0f);
        model.rotateZ(progress * -360.0f * 2.0f);
        size = (cosf(angle * 0.9f) * 0.1f + 0.2f) * (radius + 0.2f) * scale;
        model.scale(size, size, size);
        model.translate(plasma::Vector<3, float>(-0.5f, -0.5f, -0.5f));
        m_shader->setMatrices(model, view, projection);
        m_shader->setMaterialColor(middleColor);
        m_cubeSprite->draw();

        radius *= 1.01f;
        angle = (float)((phase * 3.0 * length - time * 0.006f * 3.141592653589793) * frequency);
        offset = center + side * radius * cosf(angle) + up * radius * sinf(angle);
        offset = side * radius * 0.5f * cosf(angle * 0.3f) + up * radius * 0.5f * sinf(angle * 0.3f) + offset;
        model.identity();
        model.translate(offset);
        model.rotate(rotation[2], plasma::Vector<3, float>(0.0f, 0.0f, 1.0f));
        model.rotateX(progress * 360.0f * 3.0f);
        model.rotateY(progress * 360.0f * 3.0f);
        model.rotateZ(progress * -360.0f * 3.0f);
        size = (cosf(angle * 0.9f) * 0.1f + 0.2f) * (radius + 0.2f) * scale;
        model.scale(size, size, size);
        model.translate(plasma::Vector<3, float>(-0.5f, -0.5f, -0.5f));
        m_shader->setMatrices(model, view, projection);
        m_shader->setMaterialColor(outerColor);
        m_cubeSprite->draw();
    }
}

// As drawSpriteModel, with the model scaled to its largest dimension (a ring is turned to lie
// flat) and the item's spirit cubes drawn over it. An item without a category shows the unknown
// item's sprite.
void GameController::drawItem(float x, float y, const plasma::Vector<3, float> &rotation, float scale, const Item *item,
                              float depth)
{
    if (!item)
        return;
    Sprite *sprite = m_world.m_spriteManager.itemSprite(item);
    if (item->category == ItemCategoryNone)
        sprite = m_world.m_spriteManager.sprite(SpriteUnknown);
    if (!sprite)
        return;
    m_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    m_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
    int size = std::max(sprite->sizeY(), sprite->sizeX());
    size = std::max(sprite->sizeZ(), size);
    plasma::Matrix<float> model;
    model.identity();
    model.translate(0.0f, 0.0f, 1.0f);
    model.rotateX(rotation[0]);
    model.rotateY(rotation[1]);
    model.rotateZ(rotation[2]);
    if (item->category == ItemCategoryRing) {
        model.rotateX(90.0f);
        model.rotateZ(-90.0f);
    }
    model.scale(scale, scale, scale);
    model.scale(1.0f / (float)size, 1.0f / (float)size, 1.0f / (float)size);
    model.translate((float)sprite->sizeX() * -0.5f, (float)sprite->sizeY() * -0.5f, (float)sprite->sizeZ() * -0.5f);
    plasma::Matrix<float> view;
    view.identity();

    plasma::Matrix<float> projection;
    projection.setPerspective(0.78539819f, -((float)m_windowWidth / (float)m_windowHeight), 0.1f, 1000.0f);
    plasma::Matrix<float> placement;
    placement.identity();
    placement.translate((x - (float)m_windowWidth * 0.5f) / (float)m_windowWidth * 2.0f,
                        (y - (float)m_windowHeight * 0.5f) / (float)m_windowHeight * -2.0f, depth);
    m_shader->apply();
    m_shader->setMatrices(model, view, placement * projection);
    m_shader->setAlpha(1.0f);
    m_shader->setShininess(item->shininess());
    sprite->draw();
    drawCubes(item, model, view, placement * projection, plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
}

// The model is turned about its centre one unit behind the origin, seen through a 45 degree
// perspective (mirrored in x, depth 0.1..1000) shifted so that it appears at the window position
// (x, y).
void GameController::drawSpriteModel(float x, float y, const plasma::Vector<3, float> &rotation, float scale,
                                     Sprite *sprite, float depth)
{
    if (!sprite)
        return;
    m_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    m_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
    plasma::Matrix<float> model;
    model.identity();
    model.translate(0.0f, 0.0f, 1.0f);
    model.rotateX(rotation[0]);
    model.rotateY(rotation[1]);
    model.rotateZ(rotation[2]);
    model.scale(scale, scale, scale);
    model.translate((float)sprite->sizeX() * -0.5f, (float)sprite->sizeY() * -0.5f, (float)sprite->sizeZ() * -0.5f);
    plasma::Matrix<float> view;
    view.identity();

    plasma::Matrix<float> projection;
    projection.setPerspective(0.78539819f, -((float)m_windowWidth / (float)m_windowHeight), 0.1f, 1000.0f);
    plasma::Matrix<float> placement;
    placement.identity();
    placement.translate((x - (float)m_windowWidth * 0.5f) / (float)m_windowWidth * 2.0f,
                        (y - (float)m_windowHeight * 0.5f) / (float)m_windowHeight * -2.0f, depth);
    m_shader->apply();
    m_shader->setMatrices(model, view, placement * projection);
    m_shader->setAlpha(1.0f);
    m_shader->setShininess(0.0f);
    sprite->draw();
}

void GameController::endUpdate()
{
    LeaveCriticalSection(&m_updateLock2);
    LeaveCriticalSection(&m_mapUpdateLock);
    LeaveCriticalSection(&m_chunkUpdateLock);
}

// A new character's equipment and inventory: four empty inventory tabs of 50 slots, the weapons of
// its class (worn and in the inventory), a gold and a silver ring, five life potions and an iron
// lamp; then full health and the formulas of level 1.
void GameController::equipNewCharacter()
{
    m_player->m_entityData.equipment = Equipment();
    m_player->m_inventory = Inventory();
    m_player->m_inventory.m_inventoryTabs.resize(InventoryTabCount);
    // The first tab only grows.
    if (m_player->m_inventory.m_inventoryTabs[InventoryTabEquipment].size() < 50)
        m_player->m_inventory.m_inventoryTabs[InventoryTabEquipment].resize(50);
    m_player->m_inventory.m_inventoryTabs[InventoryTabGeneral].resize(50);
    m_player->m_inventory.m_inventoryTabs[InventoryTabMaterials].resize(50);
    m_player->m_inventory.m_inventoryTabs[InventoryTabPets].resize(50);
    switch (m_player->m_entityData.classType) {
    case ClassWarrior: {
        m_player->m_entityData.equipment.weaponRight.category = ItemCategoryWeapon;
        m_player->m_entityData.equipment.weaponRight.id = WeaponGreatmace;
        m_player->m_entityData.equipment.weaponRight.material = ItemMaterialWood;
        m_player->m_entityData.equipment.weaponRight.level = 1;
        Item item;
        item.category = ItemCategoryWeapon;
        item.id = WeaponSword;
        item.material = ItemMaterialIron;
        item.level = 1;
        for (int i = 0; i < 2; i++)
            m_player->m_inventory.add(item);
        item.category = ItemCategoryWeapon;
        item.id = WeaponShield;
        m_player->m_inventory.add(item);
        break;
    }
    case ClassRogue: {
        m_player->m_entityData.equipment.weaponRight.category = ItemCategoryWeapon;
        m_player->m_entityData.equipment.weaponRight.id = WeaponDagger;
        m_player->m_entityData.equipment.weaponRight.material = ItemMaterialIron;
        m_player->m_entityData.equipment.weaponRight.level = 1;
        m_player->m_entityData.equipment.weaponLeft.category = ItemCategoryWeapon;
        m_player->m_entityData.equipment.weaponLeft.id = WeaponDagger;
        m_player->m_entityData.equipment.weaponLeft.material = ItemMaterialIron;
        m_player->m_entityData.equipment.weaponLeft.level = 1;
        Item item;
        item.category = ItemCategoryWeapon;
        item.id = WeaponFist;
        item.material = ItemMaterialIron;
        item.level = 1;
        for (int i = 0; i < 2; i++)
            m_player->m_inventory.add(item);
        item.category = ItemCategoryWeapon;
        item.id = WeaponLongsword;
        m_player->m_inventory.add(item);
        break;
    }
    case ClassMage: {
        m_player->m_entityData.equipment.weaponRight.category = ItemCategoryWeapon;
        m_player->m_entityData.equipment.weaponRight.id = WeaponStaff;
        m_player->m_entityData.equipment.weaponRight.material = ItemMaterialWood;
        m_player->m_entityData.equipment.weaponRight.level = 1;
        Item item;
        item.category = ItemCategoryWeapon;
        item.id = WeaponBracelet;
        item.material = ItemMaterialGold;
        item.level = 1;
        for (int i = 0; i < 2; i++)
            m_player->m_inventory.add(item);
        item.category = ItemCategoryWeapon;
        item.id = WeaponWand;
        item.material = ItemMaterialWood;
        m_player->m_inventory.add(item);
        break;
    }
    case ClassRanger: {
        m_player->m_entityData.equipment.weaponRight.category = ItemCategoryWeapon;
        m_player->m_entityData.equipment.weaponRight.id = WeaponBow;
        m_player->m_entityData.equipment.weaponRight.material = ItemMaterialWood;
        m_player->m_entityData.equipment.weaponRight.level = 1;
        Item item;
        item.category = ItemCategoryWeapon;
        item.id = WeaponCrossbow;
        item.material = ItemMaterialWood;
        item.level = 1;
        m_player->m_inventory.add(item);
        item.category = ItemCategoryWeapon;
        item.id = WeaponWand;
        item.material = ItemMaterialWood;
        m_player->m_inventory.add(item);
        break;
    }
    }
    m_player->m_entityData.equipment.ringRight.category = ItemCategoryRing;
    m_player->m_entityData.equipment.ringRight.material = ItemMaterialGold;
    m_player->m_entityData.equipment.ringLeft.category = ItemCategoryRing;
    m_player->m_entityData.equipment.ringLeft.material = ItemMaterialSilver;
    Item potion;
    potion.category = ItemCategoryConsumable;
    potion.id = ConsumableLifePotion;
    potion.level = 1;
    for (int i = 0; i < 5; i++)
        m_player->m_inventory.add(potion);
    m_player->m_entityData.equipment.lamp.category = ItemCategoryLamp;
    m_player->m_entityData.equipment.lamp.level = 1;
    m_player->m_entityData.equipment.lamp.id = LampLamp;
    m_player->m_entityData.equipment.lamp.material = ItemMaterialIron;
    m_player->m_entityData.hp = m_player->maxHP();
    m_player->m_playerData->knownFormulas.clear();
    learnFormulas(1);
}

// The text of a region's mission: the dictionary sentence for its kind, with the generated name of
// its leader (@name), its creature and its scenery as variables; the words of the first dialog
// joined with spaces, except before punctuation and after a hyphen.
std::wstring GameController::missionText(Region::Record *record)
{
    std::vector<Dialog> dialogs;
    Speech::Context context;
    context.variables[L"@name"] = m_world.randomName(record->mission.seed, record->mission.race);
    m_world.addCreature(L"creature", record->mission.race, 0, 0, context);
    m_world.addScenery(L"scenery", record, context);
    switch (record->mission.kind) {
    case MissionMonster:
        m_world.m_speech.generateDialogs(L"objective:monster", &context, dialogs);
        break;
    case MissionVillageMonster:
        m_world.m_speech.generateDialogs(L"objective:villagemonster", &context, dialogs);
        break;
    case MissionInvasion:
        m_world.m_speech.generateDialogs(L"objective:invasion", &context, dialogs);
        break;
    case MissionSceneryInvasion:
        m_world.m_speech.generateDialogs(L"objective:sceneryinvasion", &context, dialogs);
        break;
    case MissionVillageInvasion:
        m_world.m_speech.generateDialogs(L"objective:villageinvasion", &context, dialogs);
        break;
    case MissionCaveInvasion:
        m_world.m_speech.generateDialogs(L"objective:caveinvasion", &context, dialogs);
        break;
    case MissionRiverInvasion:
        m_world.m_speech.generateDialogs(L"objective:riverinvasion", &context, dialogs);
        break;
    case MissionRareBoss:
        m_world.m_speech.generateDialogs(L"objective:rareboss", &context, dialogs);
        break;
    case MissionVillageRareBoss:
        m_world.m_speech.generateDialogs(L"objective:villagerareboss", &context, dialogs);
        break;
    case MissionDungeon:
        m_world.m_speech.generateDialogs(L"objective:dungeon", &context, dialogs);
        break;
    case MissionGang:
        m_world.m_speech.generateDialogs(L"objective:gang", &context, dialogs);
        break;
    case MissionGangBoss:
        m_world.m_speech.generateDialogs(L"objective:gangboss", &context, dialogs);
        break;
    }
    bool noSpace = true;
    std::wstringstream stream;
    if (dialogs.begin() != dialogs.end()) {
        for (std::list<Dialog::Phrase>::iterator phrase = dialogs[0].phrases.begin();
             phrase != dialogs[0].phrases.end(); ++phrase) {
            bool punctuation = phrase->text == L"." || phrase->text == L"-" || phrase->text == L"," ||
                               phrase->text == L";" || phrase->text == L"!" || phrase->text == L"?" ||
                               phrase->text == L":";
            if (!noSpace && !punctuation)
                stream << L" ";
            noSpace = phrase->text == L"-";
            stream << phrase->text;
        }
    }
    return stream.str();
}

// The variables of the skill texts ("details:<skill>" etc.) at a level: every skill's figures are
// written whichever skill is shown (skill is not read). Percentages and seconds are rounded to
// hundredths; the ability figures come from the player's own cooldown, strike time, skill bonus
// and mana cost at that level. The skills' tooltips state the coefficients of the abilities'
// formulas (the bonus scales and bases, times 100 for percentages) and their durations in seconds,
// as  text does.
void GameController::getSkillDetails(int skill, int level, Speech::Context *context)
{
    std::wstringstream stream;
    stream << level;
    context->variables[L"@level"] = stream.str();
    stream.str(L"");

    // The passive skills.
    stream << roundHundredth(skillBonus(level) * 50.0f) << "%";
    context->variables[L"@health"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(ridingSpeed(level) * 100.0f) << "%";
    context->variables[L"@ridingspeed"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(climbingSpeed(level) * 100.0f) << "%";
    context->variables[L"@climbingpower"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(hangGlidingSpeed(level) * 100.0f) << "%";
    context->variables[L"@flyingspeed"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(swimmingSpeed(level) * 100.0f) << "%";
    context->variables[L"@swimmingspeed"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(bonusFromLevel(level) * 100.0f) << "%";
    context->variables[L"@boatspeed"] = stream.str();
    stream.str(L"");

    // Warrior: smash (54), cyclone (86), bulwark (101), war frenzy (102).
    stream << roundHundredth(m_player->cooldown(AbilitySmash, level) / 1000.0f);
    context->variables[L"@smashcooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilityCyclone, level) / 1000.0f);
    context->variables[L"@cyclonecooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->strikeTime(AbilityCyclone) / 1000.0f);
    context->variables[L"@cycloneduration"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilityBulwark, level) / 1000.0f);
    context->variables[L"@bulwarkcooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth((m_player->abilitySkillBonus(AbilityBulwark, level) * 0.3f + 0.25f) * 100.0f) << "%";
    context->variables[L"@bulwarkamount"] = stream.str();
    stream.str(L"");
    stream << L"10";
    context->variables[L"@bulwarkduration"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilityWarFrenzy, level) / 1000.0f);
    context->variables[L"@warfrenzycooldown"] = stream.str();
    stream.str(L"");
    stream << L"10";
    context->variables[L"@warfrenzyduration"] = stream.str();
    stream.str(L"");
    stream << roundHundredth((m_player->abilitySkillBonus(AbilityWarFrenzy, level) * 9.0f + 1.0f) * 100.0f) << "%";
    context->variables[L"@warfrenzyamount"] = stream.str();
    stream.str(L"");

    // Ranger: kick (21), retreat (50), aim (99), swiftness (100).
    stream << roundHundredth(m_player->cooldown(AbilityRangerKick, level) / 1000.0f);
    context->variables[L"@rangerkickcooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(skillBonus(level) * 100.0f) << L"%";
    context->variables[L"@rangerkickknockback"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilityRetreat, level) / 1000.0f);
    context->variables[L"@retreatcooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(skillBonus(level) * 100.0f) << L"%";
    context->variables[L"@retreatdistance"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilityAim, level) / 1000.0f);
    context->variables[L"@aimcooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(skillBonus(level) * 100.0f) << L"%";
    context->variables[L"@aimstealth"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilitySwiftness, level) / 1000.0f);
    context->variables[L"@swiftnesscooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(skillBonus(level) * 100.0f) << L"%";
    context->variables[L"@swiftnessamount"] = stream.str();
    stream.str(L"");
    stream << L"10";
    context->variables[L"@swiftnessduration"] = stream.str();
    stream.str(L"");

    // Mage: fire explosion (88), mana shield (103), teleport (49), healing stream (34).
    stream << roundHundredth(m_player->cooldown(AbilityFireExplosion, level) / 1000.0f);
    context->variables[L"@fireexplosioncooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(skillBonus(level) * 100.0f) << L"%";
    context->variables[L"@fireexplosionknockback"] = stream.str();
    stream.str(L"");
    stream << L"30";
    context->variables[L"@manashieldduration"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilityManaShield, level) / 1000.0f);
    context->variables[L"@manashieldcooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->abilitySkillBonus(AbilityManaShield, level) * 100.0f) << L"%";
    context->variables[L"@manashieldpower"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilityTeleport, level) / 1000.0f);
    context->variables[L"@teleportcooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->manaCost(AbilityHealingStream, level) * 100.0f * 8.0f);
    context->variables[L"@healingstreamcost"] = stream.str();
    stream.str(L"");

    // Rogue: intercept (48), shuriken (96), camouflage (97), sneak (79).
    stream << roundHundredth(m_player->cooldown(AbilityIntercept, level) / 1000.0f);
    context->variables[L"@interceptcooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilityShuriken, level) / 1000.0f);
    context->variables[L"@shurikencooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilityCamouflage, level) / 1000.0f);
    context->variables[L"@camouflagecooldown"] = stream.str();
    stream.str(L"");
    // The player's own camouflage points, not level.
    stream << roundHundredth((m_player->abilitySkillBonus(AbilityCamouflage, -1) * 12000.0f + 8000.0f) / 1000.0f);
    context->variables[L"@camouflageduration"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->cooldown(AbilitySneak, level) / 1000.0f);
    context->variables[L"@sneakcooldown"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->abilitySkillBonus(AbilitySneak, level) * 0.4f * 100.0f) << "%";
    context->variables[L"@sneakspeed"] = stream.str();
    stream.str(L"");
    stream << roundHundredth(m_player->abilitySkillBonus(AbilitySneak, level) * 100.0f) << "%";
    context->variables[L"@sneakstealth"] = stream.str();
}

// The ring menu's entries: every inventory slot (tab, slot) holding a consumable whose level is at
// most the player's.
std::vector<plasma::Vector<2, int>> GameController::collectUsableItems()
{
    std::vector<plasma::Vector<2, int>> items;
    for (int tab = 0; tab < (int)m_player->m_inventory.m_inventoryTabs.size(); tab++) {
        for (int slot = 0; slot < (int)m_player->m_inventory.m_inventoryTabs[tab].size(); slot++) {
            ItemStack &stack = m_player->m_inventory.m_inventoryTabs[tab][slot];
            if (stack.quantity && stack.item.category == ItemCategoryConsumable &&
                stack.item.level <= m_player->m_entityData.level)
                items.push_back(plasma::Vector<2, int>(tab, slot));
        }
    }
    return items;
}

Item *GameController::getHoveredEquipment()
{
    if (m_equipmentOpen) {
        if (m_equipmentSlots[EquipmentButtonWeaponLeft]->isPointerOver())
            return &m_player->m_entityData.equipment.weaponLeft;
        if (m_equipmentSlots[EquipmentButtonWeaponRight]->isPointerOver())
            return &m_player->m_entityData.equipment.weaponRight;
        if (m_equipmentSlots[EquipmentButtonRingLeft]->isPointerOver())
            return &m_player->m_entityData.equipment.ringLeft;
        if (m_equipmentSlots[EquipmentButtonRingRight]->isPointerOver())
            return &m_player->m_entityData.equipment.ringRight;
        if (m_equipmentSlots[EquipmentButtonNeck]->isPointerOver())
            return &m_player->m_entityData.equipment.neck;
        if (m_equipmentSlots[EquipmentButtonShoulder]->isPointerOver())
            return &m_player->m_entityData.equipment.shoulder;
        if (m_equipmentSlots[EquipmentButtonChest]->isPointerOver())
            return &m_player->m_entityData.equipment.chest;
        if (m_equipmentSlots[EquipmentButtonHands]->isPointerOver())
            return &m_player->m_entityData.equipment.hands;
        if (m_equipmentSlots[EquipmentButtonFeet]->isPointerOver())
            return &m_player->m_entityData.equipment.feet;
        if (m_equipmentSlots[EquipmentButtonLamp]->isPointerOver())
            return &m_player->m_entityData.equipment.lamp;
        if (m_equipmentSlots[EquipmentButtonSpecial]->isPointerOver())
            return &m_player->m_entityData.equipment.special;
        if (m_equipmentSlots[EquipmentButtonPet]->isPointerOver())
            return &m_player->m_entityData.equipment.pet;
    }
    return 0;
}

Item *GameController::getHoveredEnchantItem()
{
    if (m_enchantPanel->isVisible()) {
        EnchantWidget *enchant = m_enchantWidget;
        if (enchant->m_inventoryTabIndex >= 0 &&
            enchant->m_inventoryTabIndex < (int)m_player->m_inventory.m_inventoryTabs.size() &&
            enchant->m_inventoryItemIndex >= 0 &&
            enchant->m_inventoryItemIndex <
                (int)m_player->m_inventory.m_inventoryTabs[enchant->m_inventoryTabIndex].size() &&
            m_player->m_inventory.m_inventoryTabs[enchant->m_inventoryTabIndex][enchant->m_inventoryItemIndex]
                    .quantity != 0) {
            // The item's frame: 120 x 120 from the panel's pivot.
            plasma::Vector<2, float> corner =
                m_enchantPanel->m_matrix.transform(m_enchantPanel->transformation()->m_pivot.value());
            plasma::Vector<2, float> pointer = m_engine->getPointer();
            if (pointer[0] >= corner[0] && pointer[1] >= corner[1] && corner[0] + 120.0f > pointer[0] &&
                corner[1] + 120.0f > pointer[1])
                return &m_player->m_inventory
                            .m_inventoryTabs[enchant->m_inventoryTabIndex][enchant->m_inventoryItemIndex]
                            .item;
        }
    }
    return 0;
}

Item *GameController::getHoveredFormula()
{
    if (!m_craftingNode->isVisible())
        return 0;
    int tab = m_craftingWidget->m_hoveredCell[0];
    int cell = m_craftingWidget->m_hoveredCell[1];
    if (tab >= 0 && tab < (int)m_craftingInventory.m_inventoryTabs.size() && cell >= 0 &&
        cell < (int)m_craftingInventory.m_inventoryTabs[tab].size() &&
        m_craftingInventory.m_inventoryTabs[tab][cell].quantity >= 0 &&
        m_craftingInventory.m_inventoryTabs[tab][cell].item.category != ItemCategoryNone)
        return &m_craftingInventory.m_inventoryTabs[tab][cell].item;
    return 0;
}

Item *GameController::getHoveredIngredient()
{
    if (m_blueprintNode->isVisible()) {
        if (m_blueprintPreview->m_hoveredItem.category != ItemCategoryNone)
            return &m_blueprintPreview->m_hoveredItem;
        return 0;
    }
    if (m_voxelPanel->isVisible() && m_voxelWidget->m_hoveredUpgradeIndex >= 0)
        return &m_voxelWidget->m_previewUpgradeItem;
    return 0;
}

plasma::Vector<2, int> GameController::getHoveredSlot()
{
    if (!m_inventoryNode->isVisible())
        return plasma::Vector<2, int>(-1, -1);
    if (m_inventoryWidget->GetHoveredCell()[0] >= 0 &&
        m_inventoryWidget->GetHoveredCell()[0] < (int)m_player->m_inventory.m_inventoryTabs.size() &&
        m_inventoryWidget->GetHoveredCell()[1] >= 0 &&
        m_inventoryWidget->GetHoveredCell()[1] <
            (int)m_player->m_inventory.m_inventoryTabs[m_inventoryWidget->GetHoveredCell()[0]].size() &&
        m_player->m_inventory
                .m_inventoryTabs[m_inventoryWidget->GetHoveredCell()[0]][m_inventoryWidget->GetHoveredCell()[1]]
                .quantity != 0)
        return m_inventoryWidget->GetHoveredCell();
    return plasma::Vector<2, int>(-1, -1);
}

Item *GameController::getHoveredVendorItem()
{
    if (!m_tradePanel->isVisible())
        return 0;
    int tab = m_tradeWidget->m_hoveredCell[0];
    int cell = m_tradeWidget->m_hoveredCell[1];
    if (tab >= 0 && tab < (int)m_tradeInventory.m_inventoryTabs.size() && cell >= 0 &&
        cell < (int)m_tradeInventory.m_inventoryTabs[tab].size())
        return &m_tradeInventory.m_inventoryTabs[tab][cell].item;
    return 0;
}

// The pointer's height when a button went down; the mouse move handler measures from it.
static float s_pressedPointerY;

// A button went down (0 left, 1 right, 2 middle). The left button works the start and game menus,
// the skill screen (spending points, learning or resetting them), the crafting list, the spirit
// cubes, the equipment slots (exchanged with the stack carried by the pointer), the inventory, the
// adaption and the identifier. The right and middle buttons pick a stack for the adaption or
// identifier panel, sell it to a vendor, put it in the voxel editor, or equip or use it; on an
// equipment slot they take the item off (or sell it), and on a vendor's offer they buy it.
void GameController::handleButtonDown(int button)
// The chat's warnings are red (RGBA, 0..1).
{
    Controller::handleButtonDown(button);
    s_pressedPointerY = m_engine->getPointer()[1];
    if (isCursorVisible()) {
        switch (button) {
        case plasma::MOUSE_BUTTON_LEFT:
            m_engine->pointerPressed();
            break;
        case plasma::MOUSE_BUTTON_RIGHT:
            m_engine->rightButtonPressed();
            break;
        }
    }
    if (m_startScreen->isVisible() && m_startMenuNode->isVisible() && button == plasma::MOUSE_BUTTON_LEFT &&
        m_startMenuWidget->m_hoveredButton >= 0) {
        switch (m_startMenuWidget->m_hoveredButton) {
        case StartMenuPlay:
            m_startScreen->setVisible(false);
            m_characterListScreen->setVisible(true);
            break;
        case StartMenuOptions:
            m_optionsPanel->setVisible(true);
            break;
        case StartMenuExit:
            m_quit = true;
            break;
        }
    }
    m_characterListScrollSpeed = 0;
    // The right button on nothing closes the panels.
    if (button == plasma::MOUSE_BUTTON_RIGHT && !m_engine->getPointerNode()) {
        m_equipmentOpen = false;
        m_skillsOpen = false;
        m_gameMenuOpen = false;
        m_inventoryNode->setVisible(false);
        m_craftingNode->setVisible(false);
        m_voxelPanel->setVisible(false);
    }
    if (m_gameMenuOpen && button == plasma::MOUSE_BUTTON_LEFT) {
        for (int i = 0; i < (int)m_menuButtons.size(); i++) {
            if (m_menuButtons[i]->widget()->isPointerOver()) {
                switch (i) {
                case GameMenuHelp:
                    m_helpNode->setVisible(!m_helpNode->isVisible());
                    break;
                case GameMenuSkills:
                    toggleSkillWidget();
                    break;
                case GameMenuCrafting:
                    toggleCraftingWidget();
                    break;
                case GameMenuInventory:
                    toggleInventoryWidget();
                    break;
                case GameMenuWorldMap:
                    m_mapOpen = true;
                    break;
                case GameMenuSystem:
                    toggleSystemWidget();
                    break;
                }
            }
        }
    }
    if (m_skillsOpen) {
        if (button == plasma::MOUSE_BUTTON_LEFT) {
            for (int i = 0; i < PlayerSpecializationCount; i++) {
                if (m_specializationButtons[i]->widget()->isPointerOver()) {
                    m_skillWidget->m_specialization = i;
                    playSound(SoundMenuSelect, m_cameraPosition, 1.0f, 1.0f);
                }
            }
        }
        // The left button raises a skill while points are left (skills 1, 3, 5, 7, 8 and 9 need five
        // points in the one before them), the right button lowers it (not below what the next one needs).
        for (int i = 0; i < SkillCount; i++) {
            if (m_skillButtons[i]->widget()->isPointerOver()) {
                if (button == plasma::MOUSE_BUTTON_LEFT) {
                    int points = m_player->m_entityData.manaCubeCount / ManaCubesPerSkillPoint +
                                 m_player->m_entityData.level * SkillPointsPerLevel - SkillPointsPerLevel;
                    int spent = 0;
                    for (int j = 0; j < SkillCount; j++)
                        spent += m_skillWidget->m_skillLevels[j];
                    if (spent < points) {
                        // Skills 1, 3, 5, 7, 8 and 9 need five points in the one before them.
                        SkillWidget *skills = m_skillWidget;
                        if ((i != SkillClassAbilitySecond || skills->m_skillLevels[SkillClassAbilityFirst] >= 5) &&
                            (i != SkillClassAbilityThird || skills->m_skillLevels[SkillClassAbilitySecond] >= 5) &&
                            (i != SkillClassAbilityFourth || skills->m_skillLevels[SkillClassAbilityThird] >= 5) &&
                            (i != SkillPetRiding || skills->m_skillLevels[SkillPetTaming] >= 5) &&
                            (i != SkillHangGliding || skills->m_skillLevels[SkillClimbing] >= 5) &&
                            (i != SkillBoatDriving || skills->m_skillLevels[SkillSwimming] >= 5)) {
                            m_skillWidget->m_skillLevels[i]++;
                            playSound(SoundMenuSelect, m_cameraPosition, 1.0f, 1.0f);
                        }
                    }
                } else {
                    // A skill the next one needs keeps five points.
                    SkillWidget *skills = m_skillWidget;
                    int level = skills->m_skillLevels[i];
                    if (level > 5 ||
                        ((i != SkillClassAbilityFirst || skills->m_skillLevels[SkillClassAbilitySecond] <= 0) &&
                         (i != SkillClassAbilitySecond || skills->m_skillLevels[SkillClassAbilityThird] <= 0) &&
                         (i != SkillClassAbilityThird || skills->m_skillLevels[SkillClassAbilityFourth] <= 0) &&
                         (i != SkillPetTaming || skills->m_skillLevels[SkillPetRiding] <= 0) &&
                         (i != SkillClimbing || skills->m_skillLevels[SkillHangGliding] <= 0) &&
                         (i != SkillSwimming || skills->m_skillLevels[SkillBoatDriving] <= 0))) {
                        m_skillWidget->m_skillLevels[i] = level - 1;
                        playSound(SoundMenuDropItem, m_cameraPosition, 1.0f, 1.0f);
                        if (m_skillWidget->m_skillLevels[i] < 0)
                            m_skillWidget->m_skillLevels[i] = 0;
                    }
                }
            }
        }
    }

    if (button == plasma::MOUSE_BUTTON_LEFT) {
        // The crafting list shows the formula of the selected item.
        if (m_craftingNode->isVisible()) {
            m_craftingWidget->updateSelection(true);
            InventoryWidget *crafting = m_craftingWidget;
            if (crafting->m_selectedCell[0] >= 0 &&
                crafting->m_selectedCell[0] < (int)m_craftingInventory.m_inventoryTabs.size() &&
                crafting->m_selectedCell[1] >= 0 &&
                crafting->m_selectedCell[1] <
                    (int)m_craftingInventory.m_inventoryTabs[crafting->m_selectedCell[0]].size()) {
                Formula formula;
                if (m_world.findFormula(
                        m_craftingInventory
                            .m_inventoryTabs[m_craftingWidget->m_selectedCell[0]][m_craftingWidget->m_selectedCell[1]]
                            .item,
                        formula))
                    m_blueprintPreview->m_formula = formula;
                else
                    m_blueprintPreview->m_formula = Formula();
            } else {
                m_blueprintPreview->m_formula = Formula();
            }
        }
        if (m_voxelPanel->isVisible()) {
            VoxelWidget *voxelWidget = m_voxelWidget;
            if (voxelWidget->m_item) {
                if (voxelWidget->GetHoveredUpgradeItem())
                    voxelWidget->selectUpgrade();
                else
                    voxelWidget->placeUpgrade(voxelWidget->m_item->material, voxelWidget->m_item->level);
            }
        }
        // An equipment slot exchanges its item with the carried stack's when the carried item fits the
        // slot (a two-handed weapon empties the other hand; the pet slot feeds a carried stack of pet
        // food).
        if (m_equipmentOpen) {
            Item *hovered = getHoveredEquipment();
            if (m_player->m_inventory.m_cursor.item.category != ItemCategoryNone &&
                !m_player->canEquipItem(m_player->m_inventory.m_cursor.item) && hovered) {
                playSound(SoundHumanVoice02, m_cameraPosition, 1.0f, 1.0f);
            } else {
                bool swapped = false;
                if (hovered == &m_player->m_entityData.equipment.weaponRight &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryWeapon)) {
                    if (m_player->m_inventory.m_cursor.item.isTwoHanded() &&
                        m_player->m_entityData.equipment.weaponLeft.category != ItemCategoryNone) {
                        m_player->m_inventory.add(m_player->m_entityData.equipment.weaponLeft);
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        m_player->m_entityData.equipment.weaponLeft = Item();
                    }
                    std::swap(m_player->m_entityData.equipment.weaponRight, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (hovered == &m_player->m_entityData.equipment.weaponLeft) {
                    if (m_player->m_inventory.m_cursor.item.isTwoHanded() &&
                        m_player->m_inventory.m_cursor.item.id != WeaponBow &&
                        m_player->m_inventory.m_cursor.item.id != WeaponCrossbow) {
                        playSound(SoundHumanVoice02, m_cameraPosition, 1.0f, 1.0f);
                    } else if (m_player->m_inventory.m_cursor.quantity == 0 ||
                               m_player->m_inventory.m_cursor.item.category == ItemCategoryWeapon) {
                        if ((m_player->m_inventory.m_cursor.item.isTwoHanded() ||
                             m_player->m_entityData.equipment.weaponRight.isTwoHanded()) &&
                            m_player->m_entityData.equipment.weaponRight.category != ItemCategoryNone) {
                            m_player->m_inventory.add(m_player->m_entityData.equipment.weaponRight);
                            m_inventoryWidget->updateRows();
                            m_inventoryWidget->updateScrollButton();
                            m_player->m_entityData.equipment.weaponRight = Item();
                        }
                        std::swap(m_player->m_entityData.equipment.weaponLeft, m_player->m_inventory.m_cursor.item);
                        swapped = true;
                    }
                }
                if (hovered == &m_player->m_entityData.equipment.pet &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryPet ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryPetFood)) {
                    if (m_player->m_inventory.m_cursor.quantity > 1 &&
                        m_player->m_inventory.m_cursor.item.category == ItemCategoryPetFood) {
                        if (!(m_player->m_inventory.m_cursor.item == m_player->m_entityData.equipment.pet)) {
                            if (m_player->m_entityData.equipment.pet.category != ItemCategoryNone)
                                m_player->m_inventory.add(m_player->m_entityData.equipment.pet);
                            m_player->m_entityData.equipment.pet = m_player->m_inventory.m_cursor.item;
                            m_player->m_inventory.m_cursor.removeOne();
                        }
                    } else {
                        std::swap(m_player->m_entityData.equipment.pet, m_player->m_inventory.m_cursor.item);
                        swapped = true;
                    }
                }
                if (hovered == &m_player->m_entityData.equipment.neck &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryNeck)) {
                    std::swap(m_player->m_entityData.equipment.neck, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (hovered == &m_player->m_entityData.equipment.ringLeft &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryRing)) {
                    std::swap(m_player->m_entityData.equipment.ringLeft, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (hovered == &m_player->m_entityData.equipment.ringRight &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryRing)) {
                    std::swap(m_player->m_entityData.equipment.ringRight, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (hovered == &m_player->m_entityData.equipment.shoulder &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryShoulder)) {
                    std::swap(m_player->m_entityData.equipment.shoulder, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (hovered == &m_player->m_entityData.equipment.special &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategorySpecial)) {
                    std::swap(m_player->m_entityData.equipment.special, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (hovered == &m_player->m_entityData.equipment.lamp &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryLamp)) {
                    std::swap(m_player->m_entityData.equipment.lamp, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (hovered == &m_player->m_entityData.equipment.chest &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryChest)) {
                    std::swap(m_player->m_entityData.equipment.chest, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (hovered == &m_player->m_entityData.equipment.hands &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryHands)) {
                    std::swap(m_player->m_entityData.equipment.hands, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (hovered == &m_player->m_entityData.equipment.feet &&
                    (m_player->m_inventory.m_cursor.quantity == 0 ||
                     m_player->m_inventory.m_cursor.item.category == ItemCategoryFeet)) {
                    std::swap(m_player->m_entityData.equipment.feet, m_player->m_inventory.m_cursor.item);
                    swapped = true;
                }
                if (swapped) {
                    if (m_player->m_inventory.m_cursor.item.category != ItemCategoryNone)
                        m_player->m_inventory.m_cursor.quantity = 1;
                    else
                        m_player->m_inventory.m_cursor.removeOne();
                    playSound(m_player->m_inventory.m_cursor.quantity ? SoundMenuGrabItem : SoundMenuDropItem,
                              m_cameraPosition, 1.0f, 1.0f);
                }
            }
        }
        // An inventory cell takes the carried stack (of its tab): it adds to an equal stackable stack,
        // otherwise the two are exchanged (not while shift is held).
        if (m_inventoryNode->isVisible()) {
            InventoryWidget *inventory = m_inventoryWidget;
            if (inventory->m_hoveredCell[0] >= 0 &&
                inventory->m_hoveredCell[0] < (int)m_player->m_inventory.m_inventoryTabs.size() &&
                inventory->m_hoveredCell[1] >= 0 &&
                inventory->m_hoveredCell[1] <
                    (int)m_player->m_inventory.m_inventoryTabs[inventory->m_hoveredCell[0]].size()) {
                ItemStack &cursor = m_player->m_inventory.m_cursor;
                int carried = cursor.quantity;
                if (carried != 0 && m_player->m_inventory.itemTab(cursor.item) != inventory->m_tabIndex) {
                    playSound(SoundHumanVoice02, m_cameraPosition, 1.0f, 1.0f);
                } else {
                    if (carried != 0 && cursor.item.isStackable() &&
                        cursor.item == m_player->m_inventory
                                           .m_inventoryTabs[inventory->m_hoveredCell[0]][inventory->m_hoveredCell[1]]
                                           .item) {
                        m_player->m_inventory.m_inventoryTabs[inventory->m_hoveredCell[0]][inventory->m_hoveredCell[1]]
                            .quantity += carried;
                        m_player->m_inventory.m_cursor.quantity = 0;
                        m_player->m_inventory.m_cursor.item.category = ItemCategoryNone;
                        m_player->m_inventory.m_cursor.item.id = 0;
                    } else if (!m_keys[VK_SHIFT]) {
                        std::swap(m_player->m_inventory
                                      .m_inventoryTabs[inventory->m_hoveredCell[0]][inventory->m_hoveredCell[1]],
                                  cursor);
                        playSound(m_player->m_inventory.m_cursor.quantity ? SoundMenuGrabItem : SoundMenuDropItem);
                    }
                }
            }
        }
        // The adaption panel's upgrade sets the item to the player's level for platinum coins.
        if (m_adaptionPanel->isVisible() && m_adaptionWidget->isPointerOverUpgrade() && m_adaptionWidget->getItem() &&
            m_player->m_inventory.m_platinumCoins >= m_adaptionWidget->getCost()) {
            m_player->m_inventory.m_platinumCoins -= m_adaptionWidget->getCost();
            m_adaptionWidget->getItem()->level = m_player->m_entityData.level;
            m_adaptionWidget->getItem()->flags |= (1 << ItemFlagAdapted);
            playSound(SoundDisenchant2, m_player->m_entityData.position, 1.0f, 1.0f);
        }
        if (m_skillsOpen) {
            if (m_skillWidget->isPointerOverLearn() &&
                m_skillWidget->getCost() <= m_player->m_inventory.m_copperBalance) {
                m_player->m_inventory.m_copperBalance -= m_skillWidget->getCost();
                for (int i = 0; i < SkillCount; i++)
                    m_player->m_entityData.skillLevels[i] = m_skillWidget->m_skillLevels[i];
                m_player->m_entityData.specialization = m_skillWidget->m_specialization;
            }
            if (m_skillWidget->isPointerOverCancel()) {
                for (int i = 0; i < SkillCount; i++)
                    m_skillWidget->m_skillLevels[i] = m_player->m_entityData.skillLevels[i];
                m_skillWidget->m_specialization = m_player->m_entityData.specialization;
            }
        }
        // The identifier: identifying turns the unidentified item into a random weapon or armour of its
        // level and rarity; otherwise it is broken into five to ten spirit cubes of its material.
        if (m_enchantPanel->isVisible() && m_enchantWidget->isPointerOverIdentify() &&
            m_player->m_inventory.m_copperBalance >= m_enchantWidget->getCost()) {
            EnchantWidget *enchant = m_enchantWidget;
            if (enchant->m_canIdentify == 1) {
                if (enchant->m_inventoryTabIndex >= 0 &&
                    enchant->m_inventoryTabIndex < (int)m_player->m_inventory.m_inventoryTabs.size() &&
                    enchant->m_inventoryItemIndex >= 0 &&
                    enchant->m_inventoryItemIndex <
                        (int)m_player->m_inventory.m_inventoryTabs[enchant->m_inventoryTabIndex].size() &&
                    m_player->m_inventory.m_inventoryTabs[enchant->m_inventoryTabIndex][enchant->m_inventoryItemIndex]
                            .quantity != 0 &&
                    enchant->isUnidentifiedItem()) {
                    m_player->m_inventory.m_copperBalance -= m_enchantWidget->getCost();
                    ItemStack &stack = m_player->m_inventory.m_inventoryTabs[m_enchantWidget->m_inventoryTabIndex]
                                                                            [m_enchantWidget->m_inventoryItemIndex];
                    stack.removeOne();
                    Item item = rand() % 2 == 0 ? m_world.randomWeapon(stack.item.level, stack.item.rarity, -1)
                                                : m_world.randomArmor(stack.item.level, stack.item.rarity, -1);
                    m_player->m_inventory.add(item);
                    m_inventoryWidget->updateRows();
                    m_inventoryWidget->updateScrollButton();
                    playSound(SoundDisenchant2, m_player->m_entityData.position, 1.0f, 1.0f);
                    notifyItem(item, m_player);
                }
            } else if (enchant->m_canIdentify == 0) {
                if (enchant->m_inventoryTabIndex >= 0 &&
                    enchant->m_inventoryTabIndex < (int)m_player->m_inventory.m_inventoryTabs.size() &&
                    enchant->m_inventoryItemIndex >= 0 &&
                    enchant->m_inventoryItemIndex <
                        (int)m_player->m_inventory.m_inventoryTabs[enchant->m_inventoryTabIndex].size() &&
                    m_player->m_inventory.m_inventoryTabs[enchant->m_inventoryTabIndex][enchant->m_inventoryItemIndex]
                            .quantity != 0 &&
                    enchant->isUnidentifiedItem()) {
                    ItemStack &stack = m_player->m_inventory.m_inventoryTabs[m_enchantWidget->m_inventoryTabIndex]
                                                                            [m_enchantWidget->m_inventoryItemIndex];
                    if (stack.item.material != 0) {
                        stack.removeOne();
                        if (m_player->m_inventory
                                .m_inventoryTabs[m_enchantWidget->m_inventoryTabIndex]
                                                [m_enchantWidget->m_inventoryItemIndex]
                                .quantity != 0)
                            m_enchantWidget->SetInventoryItem(-1, -1);
                        int count = rand() % 6 + 5;
                        Item spirit;
                        spirit.category = ItemCategoryCube;
                        spirit.material = stack.item.material;
                        for (int i = 0; i < count; i++)
                            m_disenchantSpirits.push_back(spirit);
                        m_spiritTimer = 0;
                        playSound(SoundDisenchant2, m_player->m_entityData.position, 1.0f, 1.0f);
                    }
                    // After the reset above the indices are -1: this reads outside the inventory (as the original).
                    m_player->m_inventory
                        .m_inventoryTabs[m_enchantWidget->m_inventoryTabIndex][m_enchantWidget->m_inventoryItemIndex]
                        .removeOne();
                }
            }
        }
    } else if (button == plasma::MOUSE_BUTTON_RIGHT || button == plasma::MOUSE_BUTTON_MIDDLE) {
        if (button == plasma::MOUSE_BUTTON_RIGHT) {
            if (m_buildingMode)
                m_player->m_entityData.interactionState++;
            else if (m_voxelPanel->isVisible())
                m_voxelWidget->cancelUpgrade();
        }
        plasma::Vector<2, int> cell = getHoveredSlot();
        if (cell[0] >= 0 && cell[1] >= 0) {
            if (m_adaptionPanel->isVisible()) {
                m_adaptionWidget->SetInventoryItem(cell[0], cell[1]);
                m_adaptionWidget->m_item = 0;
            } else if (m_enchantPanel->isVisible()) {
                m_enchantWidget->SetInventoryItem(cell[0], cell[1]);
            } else if (m_tradePanel->isVisible()) {
                // Sold to the vendor; the last ten sold stay buyable.
                Item item = m_player->m_inventory.m_inventoryTabs[cell[0]][cell[1]].item;
                m_player->m_inventory.m_inventoryTabs[cell[0]][cell[1]].removeOne();
                m_player->m_inventory.m_copperBalance += item.price();
                playSound(SoundDropCoin, m_player->m_entityData.position, 0.75f, 1.0f);
                ItemStack sold;
                sold.quantity = 1;
                sold.item = item;
                m_soldItems.push_back(sold);
                if (m_soldItems.size() > 10)
                    m_soldItems.pop_front();
                updateVendor();
            } else if (m_voxelPanel->isVisible()) {
                Item *item = &m_player->m_inventory.m_inventoryTabs[cell[0]][cell[1]].item;
                if (item->category == ItemCategoryWeapon) {
                    m_voxelWidget->m_item = item;
                    m_voxelWidget->cancelUpgrade();
                } else {
                    playSound(SoundHumanVoice01, m_cameraPosition, 1.0f, 1.0f);
                }
            } else {
                ItemStack &stack = m_player->m_inventory.m_inventoryTabs[cell[0]][cell[1]];
                if (!m_player->canEquipItem(stack.item)) {
                    playSound(SoundHumanVoice02, m_cameraPosition, 1.0f, 1.0f);
                } else if (stack.quantity != 0) {
                    // Shields and arrows go to the left hand, bows and crossbows with the right button to the left
                    // hand with the right one emptied; other weapons to the right hand with the right button or when
                    // two-handed (emptying the left one), otherwise to the left hand.
                    if (stack.item.category == ItemCategoryWeapon) {
                        Item item = stack.item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        if (item.id == WeaponShield || item.id == WeaponArrows) {
                            if (m_player->m_entityData.equipment.weaponRight.isTwoHanded()) {
                                m_player->m_inventory.add(m_player->m_entityData.equipment.weaponRight);
                                m_player->m_entityData.equipment.weaponRight = Item();
                            }
                            m_player->m_inventory.add(m_player->m_entityData.equipment.weaponLeft);
                            m_player->m_entityData.equipment.weaponLeft = item;
                            m_inventoryWidget->updateRows();
                            m_inventoryWidget->updateScrollButton();
                            playSound(SoundMenuDropItem);
                        } else if (button == plasma::MOUSE_BUTTON_RIGHT &&
                                   (item.id == WeaponBow || item.id == WeaponCrossbow)) {
                            m_player->m_inventory.add(m_player->m_entityData.equipment.weaponLeft);
                            m_player->m_entityData.equipment.weaponLeft = item;
                            m_player->m_inventory.add(m_player->m_entityData.equipment.weaponRight);
                            m_player->m_entityData.equipment.weaponRight = Item();
                            m_inventoryWidget->updateRows();
                            m_inventoryWidget->updateScrollButton();
                            playSound(SoundMenuDropItem);
                        } else if (button == plasma::MOUSE_BUTTON_RIGHT || item.isTwoHanded()) {
                            m_player->m_inventory.add(m_player->m_entityData.equipment.weaponRight);
                            m_player->m_entityData.equipment.weaponRight = item;
                            if (item.isTwoHanded()) {
                                m_player->m_inventory.add(m_player->m_entityData.equipment.weaponLeft);
                                m_player->m_entityData.equipment.weaponLeft = Item();
                            }
                            m_inventoryWidget->updateRows();
                            m_inventoryWidget->updateScrollButton();
                            playSound(SoundMenuDropItem);
                        } else if (button == plasma::MOUSE_BUTTON_MIDDLE) {
                            m_player->m_inventory.add(m_player->m_entityData.equipment.weaponLeft);
                            m_player->m_entityData.equipment.weaponLeft = item;
                            m_inventoryWidget->updateRows();
                            m_inventoryWidget->updateScrollButton();
                            playSound(SoundMenuDropItem);
                        }
                    }
                    if (stack.item.category == ItemCategoryPet ||
                        (stack.item.category == ItemCategoryPetFood &&
                         !(stack.item == m_player->m_entityData.equipment.pet))) {
                        Item item = stack.item;
                        m_player->m_inventory.add(m_player->m_entityData.equipment.pet);
                        m_player->m_entityData.equipment.pet = item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem, m_cameraPosition, 1.0f, 1.0f);
                    }
                    if (stack.item.category == ItemCategoryChest) {
                        Item item = stack.item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        m_player->m_inventory.add(m_player->m_entityData.equipment.chest);
                        m_player->m_entityData.equipment.chest = item;
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem);
                    }
                    if (stack.item.category == ItemCategoryShoulder) {
                        Item item = stack.item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        m_player->m_inventory.add(m_player->m_entityData.equipment.shoulder);
                        m_player->m_entityData.equipment.shoulder = item;
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem);
                    }
                    if (stack.item.category == ItemCategoryNeck) {
                        Item item = stack.item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        m_player->m_inventory.add(m_player->m_entityData.equipment.neck);
                        m_player->m_entityData.equipment.neck = item;
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem);
                    }
                    if (stack.item.category == ItemCategoryRing) {
                        Item item = stack.item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        if (button == plasma::MOUSE_BUTTON_RIGHT) {
                            m_player->m_inventory.add(m_player->m_entityData.equipment.ringRight);
                            m_player->m_entityData.equipment.ringRight = item;
                        } else {
                            m_player->m_inventory.add(m_player->m_entityData.equipment.ringLeft);
                            m_player->m_entityData.equipment.ringLeft = item;
                        }
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem);
                    }
                    if (stack.item.category == ItemCategorySpecial) {
                        Item item = stack.item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        m_player->m_inventory.add(m_player->m_entityData.equipment.special);
                        m_player->m_entityData.equipment.special = item;
                        m_player->m_entityData.currentAbility = AbilityNone;
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem);
                    }
                    if (stack.item.category == ItemCategoryLamp) {
                        Item item = stack.item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        m_player->m_inventory.add(m_player->m_entityData.equipment.lamp);
                        m_player->m_entityData.equipment.lamp = item;
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem);
                    }
                    if (stack.item.category == ItemCategoryHands) {
                        Item item = stack.item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        m_player->m_inventory.add(m_player->m_entityData.equipment.hands);
                        m_player->m_entityData.equipment.hands = item;
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem);
                    }
                    if (stack.item.category == ItemCategoryFeet) {
                        Item item = stack.item;
                        stack.quantity--;
                        if (stack.quantity <= 0) {
                            stack.quantity = 0;
                            stack.item.category = ItemCategoryNone;
                            stack.item.id = 0;
                        }
                        m_player->m_inventory.add(m_player->m_entityData.equipment.feet);
                        m_player->m_entityData.equipment.feet = item;
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem);
                    }
                    // Two of a material make one of the next level.
                    if (stack.item.category == ItemCategoryMaterial && stack.quantity > 1) {
                        Item item = stack.item;
                        item.level++;
                        stack.removeOne();
                        stack.removeOne();
                        m_player->m_inventory.add(item);
                        m_inventoryWidget->updateRows();
                        m_inventoryWidget->updateScrollButton();
                        playSound(SoundMenuDropItem);
                    }
                    if (stack.item.category == ItemCategoryConsumable) {
                        m_world.enterRegions();
                        useItem(&stack);
                        m_world.leaveRegions();
                    } else if (stack.item.category == ItemCategoryFormula) {
                        // A formula item teaches the formula of its item.
                        Item formula = stack.item;
                        formula.category = stack.item.formulaCategory;
                        formula.formulaCategory = ItemCategoryNone;
                        if (!m_player->knowsFormula(formula)) {
                            m_player->m_playerData->knownFormulas.push_back(formula);
                            stack.removeOne();
                            playSound(SoundUpgrade2, m_cameraPosition, 1.0f, 1.0f);
                        } else {
                            playSound(SoundHumanVoice01, m_cameraPosition, 1.0f, 1.0f);
                        }
                    }
                }
            }
        } else {
            Item *hovered = getHoveredEquipment();
            if (hovered && hovered->category != ItemCategoryNone) {
                if (m_adaptionPanel->isVisible()) {
                    m_adaptionWidget->SetInventoryItem(-1, -1);
                    m_adaptionWidget->m_item = hovered;
                } else if (m_tradePanel->isVisible()) {
                    m_player->m_inventory.m_copperBalance += hovered->price();
                    playSound(SoundDropCoin, m_player->m_entityData.position, 0.75f, 1.0f);
                    ItemStack sold;
                    sold.quantity = 1;
                    sold.item = *hovered;
                    m_soldItems.push_back(sold);
                    if (m_soldItems.size() > 10)
                        m_soldItems.pop_front();
                    updateVendor();
                    hovered->category = ItemCategoryNone;
                    hovered->id = 0;
                } else if (m_voxelPanel->isVisible()) {
                    if (hovered->category == ItemCategoryWeapon) {
                        m_voxelWidget->m_item = hovered;
                        m_voxelWidget->cancelUpgrade();
                    } else {
                        playSound(SoundHumanVoice01, m_cameraPosition, 1.0f, 1.0f);
                    }
                } else {
                    m_player->m_inventory.add(*hovered);
                    hovered->category = ItemCategoryNone;
                    hovered->id = 0;
                    m_inventoryWidget->updateRows();
                    m_inventoryWidget->updateScrollButton();
                    playSound(SoundMenuDropItem, m_cameraPosition, 1.0f, 1.0f);
                }
            } else {
                Item *offer = getHoveredVendorItem();
                if (offer) {
                    if (m_tradeWidget->m_tabIndex == VendorTabBuyback) {
                        // Buying back: the sold stacks are listed newest first; as many as the player can carry and pay.
                        int index = (int)m_soldItems.size() - 1;
                        for (std::list<ItemStack>::iterator it = m_soldItems.begin(); it != m_soldItems.end();
                             ++it, --index) {
                            if (index == m_tradeWidget->m_hoveredCell[1]) {
                                int bought = 0;
                                for (int i = 0; i < it->quantity; i++) {
                                    if (m_player->canTakeItem(it->item)) {
                                        if (m_player->m_inventory.m_copperBalance >= offer->price()) {
                                            m_player->m_inventory.add(it->item);
                                            m_player->m_inventory.m_copperBalance -= it->item.price();
                                            bought++;
                                        }
                                    } else {
                                        m_chatWidget->printMessage(L"You can't carry more of these items.\n",
                                                                   plasma::Vector<4, float>(1.0f, 0.2f, 0.2f, 1.0f));
                                        playSound(SoundHumanVoice01, m_player->m_entityData.position, 1.0f, 1.0f);
                                        break;
                                    }
                                }
                                it->quantity -= bought;
                                if (it->quantity == 0)
                                    m_soldItems.erase(it);
                                playSound(SoundDrop, m_player->m_entityData.position, 1.0f, 1.0f);
                                updateVendor();
                                m_inventoryWidget->updateRows();
                                m_inventoryWidget->updateScrollButton();
                                break;
                            }
                        }
                    } else if (m_player->m_inventory.m_copperBalance >= offer->price()) {
                        if (!m_player->canTakeItem(*offer)) {
                            m_chatWidget->printMessage(L"You can't carry more of these items.\n",
                                                       plasma::Vector<4, float>(1.0f, 0.2f, 0.2f, 1.0f));
                            playSound(SoundHumanVoice01, m_player->m_entityData.position, 1.0f, 1.0f);
                        } else {
                            m_player->m_inventory.add(*offer);
                            m_player->m_inventory.m_copperBalance -= offer->price();
                            playSound(SoundDrop, m_player->m_entityData.position, 1.0f, 1.0f);
                            m_inventoryWidget->updateRows();
                            m_inventoryWidget->updateScrollButton();
                        }
                    }
                }
            }
        }
        updateFormulas();
    }
}

// Releasing a button closes the identifier or adaption panel through its goodbye button; on the
// map, a second click on the selected zone moves the player to the zone's centre; in the system
// menu it picks Options, Start Menu (save, disconnect, reload the characters, the title world) or
// Exit Game.
void GameController::handleButtonUp(int button)
{
    Controller::handleButtonUp(button);
    if (isCursorVisible()) {
        switch (button) {
        case plasma::MOUSE_BUTTON_LEFT:
            m_engine->pointerReleased();
            break;
        case plasma::MOUSE_BUTTON_RIGHT:
            m_engine->rightButtonReleased();
            break;
        }
    }
    if (m_enchantPanel->isVisible() && m_enchantWidget->isPointerOverGoodbye()) {
        m_enchantPanel->setVisible(false);
        m_enchantWidget->SetInventoryItem(-1, -1);
    }
    if (m_adaptionPanel->isVisible() && m_adaptionWidget->isPointerOverGoodbye()) {
        m_adaptionPanel->setVisible(false);
        m_adaptionWidget->SetInventoryItem(-1, -1);
        m_adaptionWidget->m_item = 0;
    }
    if (button == plasma::MOUSE_BUTTON_LEFT && m_mapOpen) {
        if (m_worldMap.m_canTeleport && m_worldMap.m_selectedZone != plasma::Vector<2, int>(-1, -1) &&
            m_worldMap.m_selectedZone == m_worldMap.m_hoveredZone) {
            m_world.enterRegions();
            m_player->m_entityData.position =
                plasma::Vector<3, Fixed>(Fixed(m_worldMap.m_selectedZone[0] * 256 + 128),
                                         Fixed(m_worldMap.m_selectedZone[1] * 256 + 128), Fixed(0));
            m_world.leaveRegions();
            m_worldMap.m_selectedZone = plasma::Vector<2, int>(-1, -1);
            m_worldMap.m_hoveredZone = plasma::Vector<2, int>(-1, -1);
            m_mapOpen = false;
        } else {
            m_worldMap.m_selectedZone = m_worldMap.m_hoveredZone;
        }
    }
    if (m_systemPanel->isVisible() && button == plasma::MOUSE_BUTTON_LEFT && m_systemWidget->m_hoveredButton >= 0) {
        m_systemPanel->setVisible(false);
        m_gameMenuOpen = false;
        switch (m_systemWidget->m_hoveredButton) {
        case SystemMenuOptions:
            m_optionsPanel->setVisible(true);
            break;
        case SystemMenuStartMenu:
            saveCharacter(m_selectedCharacter, m_player);
            saveWorldInfo(m_selectedWorld, m_worlds[m_selectedWorld], true);
            disconnect();
            for (int i = 0; i < (int)m_characters.size(); ++i)
                loadCharacter(i, m_characters[i]);
            m_startScreen->setVisible(true);
            changeWorld(timeGetTime(), "");
            m_targetCameraDistance = 0;
            m_cameraDistance = 0;
            m_targetCameraRotation[0] = TitleCameraPitch;
            m_targetCameraRotation[2] = 0;
            break;
        case SystemMenuExit:
            m_quit = true;
            break;
        }
    }
}

// Keys go to the chat line while it is open: Return sends it, handling the commands /name,
// /connect, /disconnect and /namepet here; anything else is sent to the server or, offline, queued
// as a local chat message. Otherwise Return opens the chat line and the other keys toggle the
// game's panels.
void GameController::handleKeyDown(unsigned char key)
// The chat's warnings are red (RGBA, 0..1).
{
    if (m_characterCreationScreen->isVisible() || m_worldCreationScreen->isVisible() ||
        m_characterListScreen->isVisible() || m_connectScreen->isVisible() || m_worldListScreen->isVisible() ||
        m_engine->getFocusWidget())
        return;
    if (m_chatWidget->m_typeboxActive) {
        if (key == VK_RETURN) {
            m_chatWidget->m_changed = true;
            if (m_chatWidget->m_typeboxText.size() != 0) {
                std::wstringstream stream(m_chatWidget->m_typeboxText);
                std::wstring command;
                stream >> command;
                if (command == L"/name") {
                    std::wstring name;
                    stream >> name;
                    if (name.size() != 0)
                        strcpy(m_player->m_entityData.name, narrowString(name).c_str());
                } else if (command == L"/connect") {
                    std::wstring address;
                    stream >> address;
                    m_serverAddress = narrowString(address);
                    connect(m_serverAddress);
                } else if (command == L"/disconnect") {
                    disconnect();
                } else if (command == L"/namepet") {
                    std::wstring name;
                    stream >> name;
                    std::wstring::size_type length = name.size();
                    if (length >= PetNameLengthLimit) {
                        m_chatWidget->printMessage(L"Pet name too long",
                                                   plasma::Vector<4, float>(1.0f, 0.2f, 0.2f, 1.0f));
                    } else if (m_player->m_entityData.equipment.pet.category == ItemCategoryPet) {
                        // The pet item keeps its name in its spirits' materials.
                        m_world.enterRegions();
                        m_player->m_entityData.equipment.pet.spiritCount = (int)length;
                        for (std::wstring::size_type i = 0; i < length; ++i) {
                            m_player->m_entityData.equipment.pet.spirits[i].material = (unsigned char)name[i];
                            m_player->m_entityData.equipment.pet.spirits[i].position =
                                plasma::Vector<3, signed char>(0, 0, 0);
                        }
                        m_world.leaveRegions();
                    }
                } else if (m_socket) {
                    m_world.enterRegions();
                    m_outgoingChatQueue.push_back(m_chatWidget->m_typeboxText);
                    m_world.leaveRegions();
                } else {
                    ChatMessage message;
                    message.creatureId = m_player->m_id;
                    message.text = m_chatWidget->m_typeboxText;
                    m_chatMessages.push_back(message);
                }
                m_chatWidget->m_typeboxText = L"";
            }
            m_chatWidget->m_typeboxActive = false;
        } else {
            m_chatWidget->handleKeyDown(key);
        }
    } else if (key == VK_RETURN) {
        m_chatWidget->m_typeboxActive = true;
    } else {
        Controller::handleKeyDown(key);
        static plasma::Vector<3, int> unused(0, 0, 0); // never read
        switch (key) {
        case VK_ESCAPE:
            m_mapOpen = false;
            m_gameMenuOpen = !m_gameMenuOpen;
            break;
        case VK_F1:
            m_helpNode->setVisible(!m_helpNode->isVisible());
            break;
        case VK_TAB:
            m_ringMenuOpen = !m_ringMenuOpen;
            break;
        case 'B':
        case 'I':
            toggleInventoryWidget();
            break;
        case 'X':
            toggleSkillWidget();
            break;
        case 'M':
            m_mapOpen = !m_mapOpen;
            break;
        case 'V':
            m_showAllLifeBars = !m_showAllLifeBars;
            break;
        case 'C':
            toggleCraftingWidget();
            break;
        case 'F':
            m_player->m_entityData.setToggles((1 << CreatureToggleLampOn),
                                              !m_player->m_entityData.binaryToggles.lampOn);
            break;
        case 'G':
            if (m_player->m_entityData.equipment.special.category) {
                if (m_player->m_entityData.equipment.special.id == SpecialItemHangGlider) {
                    // The glider opens less than two blocks below m_fallStartHeight.
                    m_player->m_entityData.setToggles(
                        (1 << CreatureToggleGliding),
                        Fixed(m_player->m_fallStartHeight) - m_player->m_entityData.position[2] < 2 &&
                            !m_player->m_entityData.binaryToggles.gliding);
                    m_player->m_entityData.currentAbility = AbilityNone;
                }
                if (m_player->m_entityData.equipment.special.id == SpecialItemBoat) {
                    if (m_player->m_entityData.currentAbility == AbilitySailBoat) {
                        m_player->m_entityData.currentAbility = AbilityNone;
                    } else if (m_player->m_entityData.flags & (1 << CreatureFlagInWater)) {
                        m_player->m_entityData.currentAbility = AbilitySailBoat;
                        m_player->m_entityData.timeSinceAbility = 0;
                    }
                }
            } else {
                m_player->m_entityData.binaryToggles.gliding = 0;
                if (m_player->m_entityData.currentAbility == AbilitySailBoat)
                    m_player->m_entityData.currentAbility = AbilityNone;
            }
            break;
        case 'O':
            toggleSystemWidget();
            break;
        }
    }
}

void GameController::handleKeyUp(unsigned char key)
{
    if (!m_engine->getFocusWidget())
        Controller::handleKeyUp(key);
}

// Pointer motion. While the character or world list is shown and the left button is held, the
// pointer drags the list (by the smoothed vertical motion; the drag speed feeds the list's scroll
// speed). Otherwise, with the cursor shown, a left drag outside any widget turns the camera and a
// middle drag turns the upgrade preview; with the cursor hidden, the relative motion turns the
// camera. The pitch stays within 0..180 degrees.
void GameController::handleMouseMove(float x, float y)
{
    Controller::handleMouseMove(x, y);
    static int lastTime = timeGetTime();
    int now = timeGetTime();
    int milliseconds = now - lastTime;
    lastTime = now;
    static float smoothedY;
    float previousY = smoothedY;
    float factor = blendFactor(milliseconds, 0.01f);
    smoothedY = (1.0f - factor) * smoothedY + m_engine->getPointer()[1] * factor;
    float scroll = smoothedY - previousY;
    if (m_characterListScreen->isVisible() && m_engine->isButtonDown(plasma::MOUSE_BUTTON_LEFT)) {
        m_engine->movePointer(x, y);
        for (std::vector<plasma::Node *>::iterator it = m_characterRows.begin(); it != m_characterRows.end(); it++) {
            plasma::Widget *widget = (*it)->widget();
            widget->moveTo((float)(m_windowWidth / 2) - widget->viewWidth() * 0.5f, widget->viewY() + scroll, true);
        }
        m_characterListScrollSpeed += m_engine->getPointerDelta()[1] * milliseconds * ListDragSpeedGain;
        return;
    }
    if (m_worldListScreen->isVisible() && m_engine->isButtonDown(plasma::MOUSE_BUTTON_LEFT)) {
        m_engine->movePointer(x, y);
        for (std::vector<plasma::Node *>::iterator it = m_worldRows.begin(); it != m_worldRows.end(); it++) {
            plasma::Widget *widget = (*it)->widget();
            widget->moveTo((float)(m_windowWidth / 2) - widget->viewWidth() * 0.5f, widget->viewY() + scroll, true);
        }
        m_worldListScrollSpeed += m_engine->getPointerDelta()[1] * milliseconds * ListDragSpeedGain;
        return;
    }
    if (isCursorVisible()) {
        m_engine->movePointer(x, y);
        if (!m_engine->getPointerNode() && m_inputs[InputMouseLeft]) {
            float turn = m_engine->getPointerDelta()[1] * (m_options.cameraSpeed * 0.005f);
            if (m_options.invertY)
                m_targetCameraRotation[0] -= turn;
            else
                m_targetCameraRotation[0] += turn;
            if (m_targetCameraRotation[0] > 180.0f)
                m_targetCameraRotation[0] = 180.0f;
            if (m_targetCameraRotation[0] < 0.0f)
                m_targetCameraRotation[0] = 0.0f;
            m_targetCameraRotation[2] -= m_engine->getPointerDelta()[0] * (m_options.cameraSpeed * 0.005f);
        }
        if (m_inputs[InputMouseMiddle] && m_voxelPanel->isVisible()) {
            m_voxelWidget->m_rotation[2] += m_engine->getPointerDelta()[0] * 0.5f;
            m_voxelWidget->m_rotation[0] -= m_engine->getPointerDelta()[1] * 0.5f;
        }
    } else {
        m_engine->getPointerNode(); // the result is not used
        if (m_options.invertY)
            m_targetCameraRotation[0] -= m_options.cameraSpeed * 0.005f * y;
        else
            m_targetCameraRotation[0] += m_options.cameraSpeed * 0.005f * y;
        if (m_targetCameraRotation[0] > 180.0f)
            m_targetCameraRotation[0] = 180.0f;
        if (m_targetCameraRotation[0] < 0.0f)
            m_targetCameraRotation[0] = 0.0f;
        m_targetCameraRotation[2] -= m_options.cameraSpeed * 0.005f * x;
    }
}

void GameController::handleMouseWheel(int delta)
{
    if (m_mapOpen) {
        if (delta < 0)
            m_targetMapZoom *= 0.9f;
        else
            m_targetMapZoom /= 0.9f;
        if (m_targetMapZoom > MaximumMapZoom)
            m_targetMapZoom = MaximumMapZoom;
        if (m_targetMapZoom < 0.01f)
            m_targetMapZoom = 0.01f;
    } else {
        m_cameraDistance -= delta * CameraDistancePerWheelStep;
        if (m_cameraDistance < 0.0f)
            m_cameraDistance = 0.0f;
        if (m_cameraDistance > MaximumCameraDistance)
            m_cameraDistance = MaximumCameraDistance;
    }
}

bool GameController::isClassMasterNearby()
{
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         ++it) {
        Creature *creature = it->second;
        if (creature && (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagClassMaster)) &&
            creature->m_entityData.classType == m_player->m_entityData.classType) {
            plasma::Vector<3, float> offset(m_player->m_entityData.position - creature->m_entityData.position);
            if ((offset[1] * offset[1] + offset[0] * offset[0] + offset[2] * offset[2]) < ClassMasterRangeSquared)
                return true;
        }
    }
    return false;
}

bool GameController::isCursorVisible()
{
    if (m_chatWidget && m_chatWidget->m_typeboxActive)
        return true;
    if (m_mapOpen || m_equipmentOpen || m_skillsOpen)
        return true;
    if (m_systemPanel->isVisible())
        return true;
    if (m_gameMenuOpen)
        return true;
    if (m_inventoryNode && m_inventoryNode->isVisible())
        return true;
    if (m_tradePanel && m_tradePanel->isVisible())
        return true;
    if (m_enchantPanel && m_enchantPanel->isVisible())
        return true;
    if (m_adaptionPanel && m_adaptionPanel->isVisible())
        return true;
    if (m_voxelPanel && m_voxelPanel->isVisible())
        return true;
    if (m_craftingNode && m_craftingNode->isVisible())
        return true;
    if (m_characterCreationScreen->isVisible() || m_characterListScreen->isVisible() || m_startScreen->isVisible() ||
        m_worldListScreen->isVisible() || m_connectScreen->isVisible() || m_worldCreationScreen->isVisible() ||
        m_optionsPanel->isVisible())
        return true;
    return false;
}

// Whether a box (from minimum to maximum, absolute block positions) is in range of the camera
// (measured flat from its centre) and not wholly outside one of the frustum's planes (tested at
// the corner furthest along the plane's normal).
bool GameController::isBoxVisible(const plasma::Vector<3, Fixed> &minimum, const plasma::Vector<3, Fixed> &maximum,
                                  float range)
{
    float rangeSquared = range * range;
    plasma::Vector<3, Fixed> center = (minimum + maximum) * Fixed(0.5);
    plasma::Vector<2, Fixed> offset;
    offset[0] = m_cameraPosition[0] - center[0];
    offset[1] = m_cameraPosition[1] - center[1];
    Fixed squaredDistance = offset.squaredLength();
    if (squaredDistance > Fixed(rangeSquared))
        return false;
    plasma::Vector<3, Fixed> low = minimum + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0));
    plasma::Vector<3, float> lowPoint((float)low[0], (float)low[1], (float)low[2]);
    plasma::Vector<3, Fixed> high = maximum + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0));
    plasma::Vector<3, float> highPoint((float)high[0], (float)high[1], (float)high[2]);
    for (int i = 0; i < 6; i++) {
        float x = m_frustum[i][0] <= 0.0f ? lowPoint[0] : highPoint[0];
        float y = m_frustum[i][1] <= 0.0f ? lowPoint[1] : highPoint[1];
        float z = m_frustum[i][2] <= 0.0f ? lowPoint[2] : highPoint[2];
        if (m_frustum[i][0] * x + m_frustum[i][1] * y + m_frustum[i][2] * z + m_frustum[i][3] < 0.0f)
            return false;
    }
    return true;
}

// Whether a sphere is in range of the camera and not wholly outside one of the frustum's planes.
bool GameController::isSphereVisible(const plasma::Vector<3, Fixed> &position, float radius, float range)
{
    plasma::Vector<3, Fixed> offset = m_cameraPosition - position;
    Fixed squaredDistance = offset.squaredLength();
    if (squaredDistance > Fixed(range * range))
        return false;
    plasma::Vector<3, Fixed> world = position + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0));
    plasma::Vector<3, float> point((float)world[0], (float)world[1], (float)world[2]);
    for (int i = 0; i < 6; i++) {
        if (m_frustum[i][0] * point[0] + m_frustum[i][1] * point[1] + m_frustum[i][2] * point[2] + m_frustum[i][3] +
                radius <
            0.0f)
            return false;
    }
    return true;
}

bool GameController::hasHoveredTradeItem()
{
    return m_tradePanel->isVisible() && getHoveredSlot()[0] >= 0 && getHoveredSlot()[1] >= 0 &&
           m_player->m_inventory.m_inventoryTabs[getHoveredSlot()[0]][getHoveredSlot()[1]].quantity != 0;
}

// Learns formulas at a level: consumables selected by the level modulo four and five, the class's yarn and basic materials at level one, then a random armor formula for the class material and a weapon formula for the class. New characters and level changes call this function.
void GameController::learnFormulas(int level)
{
    Item item;
    item.level = level;
    item.rarity = ItemRarityWhite;
    item.material = 0;
    if (level % 4 == 1) {
        item.category = ItemCategoryConsumable;
        item.id = ConsumableLifePotion;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
    }
    if (level % 4 == 3) {
        item.category = ItemCategoryConsumable;
        item.id = ConsumableCactusPotion;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
    }
    if (level % 5 == 1) {
        item.category = ItemCategoryConsumable;
        item.id = ConsumableGinsengSoup;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
        item.category = ItemCategoryConsumable;
        item.id = ConsumablePineappleSlice;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
    }
    if (level % 5 == 2) {
        item.category = ItemCategoryConsumable;
        item.id = ConsumablePumpkinMuffin;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
    }
    if (level % 5 == 3) {
        item.category = ItemCategoryConsumable;
        item.id = ConsumableMushroomSpit;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
    }
    if (level % 5 == 4) {
        item.category = ItemCategoryConsumable;
        item.id = ConsumableSnowBerryMash;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
    }
    if (level == 1) {
        item.level = level;
        item.category = ItemCategoryMaterial;
        item.id = MaterialItemWaterFlask;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
        item.category = ItemCategoryMaterial;
        item.id = MaterialItemHeartflower;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
        if (m_player->m_entityData.classType == ClassRogue) {
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemYarn;
            item.material = ItemMaterialCotton;
            item.level = 1;
            if (!m_player->knowsFormula(item))
                m_player->m_playerData->knownFormulas.push_back(item);
        }
        if (m_player->m_entityData.classType == ClassRanger) {
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemYarn;
            item.material = ItemMaterialLinen;
            item.level = 1;
            if (!m_player->knowsFormula(item))
                m_player->m_playerData->knownFormulas.push_back(item);
        }
        if (m_player->m_entityData.classType == ClassMage) {
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemYarn;
            item.material = ItemMaterialSilk;
            item.level = 1;
            if (!m_player->knowsFormula(item))
                m_player->m_playerData->knownFormulas.push_back(item);
        }
        if (m_player->m_entityData.classType == ClassMage) {
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemYarn;
            item.material = ItemMaterialSilk;
            item.level = 1;
            if (!m_player->knowsFormula(item))
                m_player->m_playerData->knownFormulas.push_back(item);
        }
        item.category = ItemCategoryMaterial;
        item.id = MaterialItemCube;
        item.material = ItemMaterialIron;
        item.level = 1;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
        item.category = ItemCategoryMaterial;
        item.id = MaterialItemCube;
        item.material = ItemMaterialWood;
        item.level = 1;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
        item.category = ItemCategoryMaterial;
        item.id = MaterialItemCube;
        item.material = ItemMaterialGold;
        item.level = 1;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
        item.category = ItemCategoryMaterial;
        item.id = MaterialItemCube;
        item.material = ItemMaterialSilver;
        item.level = 1;
        if (!m_player->knowsFormula(item))
            m_player->m_playerData->knownFormulas.push_back(item);
    }

    // A piece of armour of the class's material, its slot by the level.
    item.level = level;
    item.id = 0;
    item.modifier = rand() % FormulaModifierCount;
    switch (level % 4) {
    case 1:
        item.category = ItemCategoryChest;
        break;
    case 2:
        item.category = ItemCategoryFeet;
        break;
    case 3:
        item.category = ItemCategoryHands;
        break;
    case 0:
        item.category = ItemCategoryShoulder;
        break;
    }
    switch (m_player->m_entityData.classType) {
    case ClassWarrior:
        item.material = ItemMaterialIron;
        break;
    case ClassRanger:
        item.material = ItemMaterialLinen;
        break;
    case ClassRogue:
        item.material = ItemMaterialCotton;
        break;
    case ClassMage:
        item.material = ItemMaterialSilk;
        break;
    }
    if (!m_player->knowsFormula(item))
        m_player->m_playerData->knownFormulas.push_back(item);

    // A weapon of the class, its kind by the level.
    item.category = ItemCategoryWeapon;
    item.modifier = rand() % 11;
    switch (m_player->m_entityData.classType) {
    case ClassWarrior:
        item.material = ItemMaterialIron;
        switch (level % 3) {
        case 0:
            item.id = rand() % WarriorWeaponKinds + WeaponGreatsword;
            break;
        case 1:
            item.id = rand() % WarriorWeaponKinds;
            break;
        case 2:
            item.id = WeaponShield;
            break;
        }
        break;
    case ClassRanger:
        item.material = ItemMaterialWood;
        switch (level % 3) {
        case 0:
            item.id = WeaponBow;
            break;
        case 1:
            item.id = WeaponCrossbow;
            break;
        case 2:
            item.id = WeaponBoomerang;
            break;
        }
        break;
    case ClassRogue:
        item.material = ItemMaterialIron;
        switch (level % 3) {
        case 0:
            item.id = WeaponDagger;
            break;
        case 1:
            item.id = WeaponLongsword;
            break;
        case 2:
            item.id = WeaponFist;
            break;
        }
        break;
    case ClassMage:
        switch (level % 3) {
        case 0:
            item.id = WeaponStaff;
            item.material = ItemMaterialWood;
            break;
        case 1:
            item.id = WeaponWand;
            item.material = ItemMaterialWood;
            break;
        case 2:
            item.id = WeaponBracelet;
            item.material = rand() % 2 ? ItemMaterialSilver : ItemMaterialGold;
            break;
        }
        break;
    }
    if (!m_player->knowsFormula(item))
        m_player->m_playerData->knownFormulas.push_back(item);
}

// Reads a character database slot into creature, restores its drawn position and remembers its level and experience.
void GameController::loadCharacter(int index, Creature *creature)
{
    m_world.enterRegions();
    plasma::Vector<3, Fixed> position = creature->m_entityData.position;
    std::stringstream key;
    key << index;
    BytesIO bytes;
    EnterCriticalSection(&m_charactersLock);
    if (m_charactersDatabase.read(key.str(), bytes)) {
        bytes.decompress();
        Serializer(&bytes).loadCreature(creature);
        creature->m_entityData.position = position;
        creature->m_drawnPosition = creature->m_entityData.position;
        creature->m_shownXp = creature->m_entityData.xp;
        creature->m_shownLevel = creature->m_entityData.level;
    }
    LeaveCriticalSection(&m_charactersLock);
    m_world.leaveRegions();
}

// Reads saved world slot index (saveWorldInfo's layout): the name, seed and explored area, and the
// compressed voxels of the preview sprite when it has a size.
void GameController::loadWorldInfo(int index, WorldInfo *world)
{
    std::stringstream key;
    key << index;
    BytesIO bytes;
    if (!m_worldsDatabase.read(key.str(), bytes))
        return;
    m_world.enterRegions();
    if (world->m_preview) {
        delete world->m_preview;
        world->m_preview = 0;
    }
    int nameLength = 0;
    bytes.read(&nameLength, sizeof(nameLength));
    world->m_name.resize(nameLength, ' ');
    bytes.read(&world->m_name[0], nameLength);
    bytes.read(&world->m_seed, sizeof(world->m_seed));
    bytes.read(&world->m_exploredArea, sizeof(world->m_exploredArea));
    plasma::Vector<3, int> size;
    bytes.read(&size, sizeof(size));
    if (size[0] > 0 && size[1] > 0 && size[2] > 0) {
        int length;
        bytes.read(&length, sizeof(length));
        if (length) {
            std::vector<char> compressed(length);
            bytes.read(&compressed[0], length);
            std::vector<char> voxels;
            decompress(compressed, voxels);
            world->m_preview = new Sprite(m_device, false);
            world->m_preview->resize(size[0], size[1], size[2]);
            memcpy(world->m_preview->m_voxels, &voxels[0], voxels.size());
            world->m_preview->updateMesh();
        }
    }
    m_world.leaveRegions();
}

// A chat line about an item a creature received: "You receive " or "<name> receives ", then coins
// as their count and metal, other items as their name (with the count before it where levels do
// not apply, the power after it where they do) in the item's colour, then ".".
void GameController::notifyItem(const Item &item, Creature *creature)
{
    if (creature == m_player)
        m_chatWidget->printMessage(L"You receive ", plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    else
        m_chatWidget->printMessage(widenString(std::string(creature->m_entityData.name)) + L" receives ",
                                   plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    std::wstringstream text;
    if (item.category == ItemCategoryCoin) {
        text << item.level << " " << m_world.m_speech.getForm(m_world.materialKey(item.material), L"singular");
        m_chatWidget->printMessage(text.str(), plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        if (!item.scalesWithLevel())
            text << item.level << L" X ";
        text << m_world.createItemName(item);
        if (item.scalesWithLevel())
            text << L" +" << item.power();
        m_chatWidget->printMessage(text.str(), item.textColor());
    }
    m_chatWidget->printMessage(L".\n", plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
}

void GameController::onBack()
{
    if (m_worldCreationScreen->isVisible()) {
        m_worldCreationScreen->setVisible(false);
        m_worldListScreen->setVisible(true);
    } else if (m_worldListScreen->isVisible()) {
        m_worldListScreen->setVisible(false);
        m_characterListScreen->setVisible(true);
    } else if (m_characterCreationScreen->isVisible()) {
        m_characterCreationScreen->setVisible(false);
        m_characterListScreen->setVisible(true);
    } else if (m_characterListScreen->isVisible()) {
        m_characterListScreen->setVisible(false);
        m_startScreen->setVisible(true);
    } else if (m_connectScreen->isVisible()) {
        m_connectScreen->setVisible(false);
        m_worldListScreen->setVisible(true);
    }
}

void GameController::onChangeInventoryTab(void *widget)
{
    if (widget == m_tradeWidget)
        updateVendor();
}

void GameController::connectToServer()
{
    std::wstring address = m_serverAddressNode->getText(L"edit", true);
    if (!address.empty()) {
        m_connectScreen->setVisible(false);
        connect(narrowString(address));
    }
}

// The character list's Delete button: the selected character goes, the saved count ("num") is
// rewritten, its slot is erased and every later slot moves down by one.
void GameController::onDeleteCharacter()
{
    delete m_characters[m_selectedCharacter];
    m_characters.erase(m_characters.begin() + m_selectedCharacter);
    BytesIO bytes;
    int count = (int)m_characters.size();
    bytes.write(&count, sizeof(count));
    m_charactersDatabase.write("num", bytes);
    std::stringstream key;
    key << m_selectedCharacter;
    m_charactersDatabase.erase(key.str());
    for (int i = m_selectedCharacter; i < count + 1; i++) {
        std::stringstream from;
        from << i + 1;
        std::stringstream to;
        to << i;
        m_charactersDatabase.rename(from.str(), to.str());
    }
    refreshCharacterList();
}

void GameController::onDeleteWorld()
{
    if (m_selectedWorld >= 0 && m_selectedWorld < (int)m_worlds.size()) {
        std::string worldPath = "Save\\world_" + m_worlds[m_selectedWorld]->m_name + ".db";
        std::string mapPath = "Save\\map_" + m_worlds[m_selectedWorld]->m_name + ".db";
        DeleteFileA(worldPath.c_str());
        DeleteFileA(mapPath.c_str());
        delete m_worlds[m_selectedWorld];
        m_worlds.erase(m_worlds.begin() + m_selectedWorld);
        if (m_selectedWorld > (int)m_worlds.size())
            m_selectedWorld = (int)m_worlds.size() - 1;
        BytesIO bytes;
        int count = (int)m_worlds.size();
        bytes.write(&count, sizeof(count));
        m_worldsDatabase.write("num", bytes);
        for (int i = 0; i < count; ++i)
            // The alpha writes each remaining world under the selected index.
            saveWorldInfo(m_selectedWorld, m_worlds[i], true);
        updateWorldList();
    }
}

void GameController::showConnectScreen()
{
    m_connectScreen->setVisible(true);
    m_worldListScreen->setVisible(false);
    m_serverAddressNode->setText(L"edit", L"", true);
    m_connectionErrorNode->setText(L"edit", L"", true);
}

// The character creation's Create button: the player creature, named from the edit, becomes a new
// saved character at level 1 (the count "num" and the slot are written), and a copy of it joins
// the character list.
void GameController::onDoneCharacter()
{
    m_characterCreationScreen->setVisible(false);
    m_worldListScreen->setVisible(true);
    std::string name = narrowString(m_characterNameNode->getText(L"edit", true));
    strcpy(m_player->m_entityData.name, name.c_str());
    m_player->m_entityData.level = 1;
    BytesIO bytes;
    int count = (int)m_characters.size() + 1;
    bytes.write(&count, sizeof(count));
    m_charactersDatabase.write("num", bytes);
    m_selectedCharacter = count - 1;
    saveCharacter(m_selectedCharacter, m_player);
    Creature *character = new Creature(0);
    character->m_entityData = m_player->m_entityData;
    character->m_inventory = m_player->m_inventory;
    character->m_drawnPosition = character->m_entityData.position;
    character->m_playerData = new Creature::PlayerData;
    static_cast<Creature::CharacterStyle &>(*character->m_playerData) = *m_player->m_playerData;
    character->m_playerData->knownFormulas = m_player->m_playerData->knownFormulas;
    m_characters.push_back(character);
    refreshCharacterList();
}

void GameController::onDoneWorld()
{
    m_worldCreationScreen->setVisible(false);
    m_cameraDistance = CreationCameraDistance;
    m_targetCameraDistance = CreationCameraDistance;
    m_targetCameraRotation = plasma::Vector<3, float>(CreationCameraPitch, 0.0f, WorldCreationCameraYaw);
    m_cameraRotation = m_targetCameraRotation;
    BytesIO bytes;
    int count = (int)m_worlds.size() + 1;
    bytes.write(&count, sizeof(count));
    m_worldsDatabase.write("num", bytes);
    std::wstring name = m_worldNameNode->getText(L"edit", true);
    std::wstring seed = m_worldSeedNode->getText(L"edit", true);
    WorldInfo *world = new WorldInfo;
    std::wstringstream(seed) >> world->m_seed;
    world->m_name = narrowString(name);
    m_worlds.push_back(world);
    m_selectedWorld = (int)m_worlds.size() - 1;
    saveWorldInfo(m_selectedWorld, world, true);
    changeWorld(m_worlds[m_selectedWorld]->m_seed, m_worlds[m_selectedWorld]->m_name);
    updateWorldList();
}

void GameController::onEditCharacter()
{
    m_deleteCharacterButton->setVisible(true);
}

void GameController::onEditWorld()
{
    m_deleteWorldButton->setVisible(true);
}

void GameController::onKeyTyped(char character)
{
    if (!m_engine->getFocusWidget() && m_chatWidget->m_typeboxActive)
        m_chatWidget->typeCharacter(character);
}

// The character list's Select button: a saved character is loaded into the player and the game
// starts; the empty slot opens the character creation with the player reset to a new warrior.
void GameController::onSelectCharacter()
{
    m_characterListScreen->setVisible(false);
    if (m_selectedCharacter >= 0 && m_selectedCharacter < (int)m_characters.size()) {
        loadCharacter(m_selectedCharacter, m_player);
        m_worldListScreen->setVisible(true);
    } else {
        m_cameraDistance = CreationCameraDistance;
        m_targetCameraDistance = CreationCameraDistance;
        m_targetCameraRotation = plasma::Vector<3, float>(CreationCameraPitch, 0.0f, 0.0f);
        m_cameraRotation = m_targetCameraRotation;
        m_characterCreationScreen->setVisible(true);
        m_player->reset();
        m_player->m_entityData.name[0] = 0;
        m_player->m_entityData.hostilityType = HostilityPlayer;
        m_player->m_entityData.rotation = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        m_player->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        m_player->m_entityData.flags = 0;
        m_player->m_entityData.multipliers.damage = 1.0f;
        m_player->m_entityData.level = 1;
        m_player->m_entityData.hp = m_player->maxHP();
        m_player->m_entityData.equipment.weaponRight.category = ItemCategoryWeapon;
        m_player->m_entityData.equipment.weaponRight.id = WeaponSword;
        m_player->m_entityData.equipment.weaponRight.material = ItemMaterialIron;
        m_player->m_entityData.equipment.weaponLeft.category = ItemCategoryWeapon;
        m_player->m_entityData.equipment.weaponLeft.id = WeaponSword;
        m_player->m_entityData.equipment.weaponLeft.material = ItemMaterialIron;
        m_player->m_entityData.equipment.chest.material = ItemMaterialIron;
        m_player->m_entityData.hostilityType = HostilityPlayer;
        m_player->m_entityData.classType = ClassWarrior;
        delete m_player->m_playerData;
        m_player->m_playerData = new Creature::PlayerData;
        m_characterNameNode->setText(L"");
        m_characterStyleWidget->reset();
        m_characterStyleWidget->updateCreature(false);
    }
}

void GameController::onSelectWorld()
{
    m_worldListScreen->setVisible(false);
    m_cameraDistance = CreationCameraDistance;
    m_targetCameraDistance = CreationCameraDistance;
    m_targetCameraRotation = plasma::Vector<3, float>(CreationCameraPitch, 0.0f, WorldCreationCameraYaw);
    m_cameraRotation = m_targetCameraRotation;
    if (m_selectedWorld < (int)m_worlds.size()) {
        WorldInfo *world = m_worlds[m_selectedWorld];
        changeWorld(world->m_seed, world->m_name);
    } else {
        m_worldCreationScreen->setVisible(true);
    }
}

void GameController::onToggleWorlds()
{
    m_showMultiplayerWorlds = !m_showMultiplayerWorlds;
    if (m_showMultiplayerWorlds)
        m_worldListButton->setText(L"Singleplayer worlds...");
    else
        m_worldListButton->setText(L"Multiplayer worlds...");
    updateWorldList();
}

// The squared distance of two world positions, in blocks.
static inline float distanceSquared(const plasma::Vector<3, Fixed> &a, const plasma::Vector<3, Fixed> &b)
{
    plasma::Vector<3, float> difference(a - b);
    return difference[0] * difference[0] + difference[1] * difference[1] + difference[2] * difference[2];
}

// Nothing beyond 100 blocks; the volume falls linearly with the distance and the sound is panned
// by the position's x in view space. The file is looked up by name (the path prefix is empty).
void GameController::playSound(int sound, const plasma::Vector<3, Fixed> &position, float volume, float speed)
{
    if (distanceSquared(position, m_cameraPosition) > 10000.0f)
        return;
    volume *= 1.0f - sqrt(distanceSquared(position, m_cameraPosition)) / 100.0f;
    std::string path;
    int id = 0;
    switch (sound) {
    case SoundHit:
        id = m_audio->soundId(path + "hit.wav");
        break;
    case SoundBlade1:
        id = m_audio->soundId(path + "blade1.wav");
        break;
    case SoundBlade2:
        id = m_audio->soundId(path + "blade2.wav");
        break;
    case SoundHitArrow:
        id = m_audio->soundId(path + "hit-arrow.wav");
        break;
    case SoundHitArrowCritical:
        id = m_audio->soundId(path + "hit-arrow-critical.wav");
        break;
    case SoundLongBlade1:
        id = m_audio->soundId(path + "long-blade1.wav");
        break;
    case SoundLongBlade2:
        id = m_audio->soundId(path + "long-blade2.wav");
        break;
    case SoundSmash1:
        id = m_audio->soundId(path + "smash1.wav");
        break;
    case SoundHit1:
        id = m_audio->soundId(path + "hit1.wav");
        break;
    case SoundHit2:
        id = m_audio->soundId(path + "hit2.wav");
        break;
    case SoundPunch1:
        id = m_audio->soundId(path + "punch1.wav");
        break;
    case SoundPunch2:
        id = m_audio->soundId(path + "punch2.wav");
        break;
    case SoundPunch282:
        id = m_audio->soundId(path + "punch2.wav");
        break;
    case SoundSlamGround:
        id = m_audio->soundId(path + "slam-ground.wav");
        break;
    case SoundSmashHit2:
        id = m_audio->soundId(path + "smash-hit2.wav");
        break;
    case SoundSmashJump:
        id = m_audio->soundId(path + "smash-jump.wav");
        break;
    case SoundSwordHit03:
        id = m_audio->soundId(path + "sword-hit03.wav");
        break;
    case SoundBlock:
        id = m_audio->soundId(path + "block.wav");
        break;
    case SoundShieldSlam:
        id = m_audio->soundId(path + "shield-slam.wav");
        break;
    case SoundSwing:
        id = m_audio->soundId(path + "swing.wav");
        break;
    case SoundShieldSwing:
        id = m_audio->soundId(path + "shield-swing.wav");
        break;
    case SoundSwingSlow:
        id = m_audio->soundId(path + "swing-slow.wav");
        break;
    case SoundSwingSlow2:
        id = m_audio->soundId(path + "swing-slow2.wav");
        break;
    case SoundSwirl:
        id = m_audio->soundId(path + "swirl.wav");
        break;
    case SoundSalvo2:
        id = m_audio->soundId(path + "salvo2.wav");
        break;
    case SoundRoll:
        id = m_audio->soundId(path + "roll.wav");
        break;
    case SoundDestroy2:
        id = m_audio->soundId(path + "destroy2.wav");
        break;
    case SoundArrowDestroy:
        id = m_audio->soundId(path + "arrow-destroy.wav");
        break;
    case SoundPoweredProjectileHit:
        id = m_audio->soundId(path + "blade1.wav");
        break;
    case SoundPunch221:
        id = m_audio->soundId(path + "punch2.wav");
        break;
    case SoundCry:
        id = m_audio->soundId(path + "cry.wav");
        break;
    case SoundLevelUp2:
        id = m_audio->soundId(path + "levelup2.wav");
        break;
    case SoundMissionComplete:
        id = m_audio->soundId(path + "missioncomplete.wav");
        break;
    case SoundWaterSplash01:
        id = m_audio->soundId(path + "water-splash01.wav");
        break;
    case SoundStep2:
        id = m_audio->soundId(path + "step2.wav");
        break;
    case SoundStepWater:
        id = m_audio->soundId(path + "step-water.wav");
        break;
    case SoundStepWater2:
        id = m_audio->soundId(path + "step-water2.wav");
        break;
    case SoundStepWater3:
        id = m_audio->soundId(path + "step-water3.wav");
        break;
    case SoundChannel2:
        id = m_audio->soundId(path + "channel2.wav");
        break;
    case SoundChannelHit:
        id = m_audio->soundId(path + "channel-hit.wav");
        break;
    case SoundFireball:
        id = m_audio->soundId(path + "fireball.wav");
        break;
    case SoundFireHit:
        id = m_audio->soundId(path + "fire-hit.wav");
        break;
    case SoundAbsorb:
        id = m_audio->soundId(path + "absorb.wav");
        break;
    case SoundManashield:
        id = m_audio->soundId(path + "manashield.wav");
        break;
    case SoundBulwark:
        id = m_audio->soundId(path + "bulwark.wav");
        break;
    case SoundMagic02:
        id = m_audio->soundId(path + "magic02.wav");
        break;
    case SoundWatersplash:
        id = m_audio->soundId(path + "watersplash.wav");
        break;
    case SoundWatersplashHit:
        id = m_audio->soundId(path + "watersplash-hit.wav");
        break;
    case SoundExplosion:
        id = m_audio->soundId(path + "Explosion.wav");
        break;
    case SoundLichScream:
        id = m_audio->soundId(path + "lich-scream.wav");
        break;
    case SoundDrink2:
        id = m_audio->soundId(path + "drink2.wav");
        break;
    case SoundPickup:
        id = m_audio->soundId(path + "pickup.wav");
        break;
    case SoundDisenchant2:
        id = m_audio->soundId(path + "disenchant2.wav");
        break;
    case SoundUpgrade2:
        id = m_audio->soundId(path + "upgrade2.wav");
        break;
    case SoundHumanVoice01:
        id = m_audio->soundId(path + "human-voice01.wav");
        break;
    case SoundHumanVoice02:
        id = m_audio->soundId(path + "human-voice02.wav");
        break;
    case SoundGate:
        id = m_audio->soundId(path + "gate.wav");
        break;
    case SoundLever:
        id = m_audio->soundId(path + "lever.wav");
        break;
    case SoundSpikeTrap:
        id = m_audio->soundId(path + "spike-trap.wav");
        break;
    case SoundFireTrap:
        id = m_audio->soundId(path + "fire-trap.wav");
        break;
    case SoundMagic0256:
        id = m_audio->soundId(path + "magic02.wav");
        break;
    case SoundCharge2:
        id = m_audio->soundId(path + "charge2.wav");
        break;
    case SoundDrop:
        id = m_audio->soundId(path + "drop.wav");
        break;
    case SoundDropCoin:
        id = m_audio->soundId(path + "drop-coin.wav");
        break;
    case SoundDropItem:
        id = m_audio->soundId(path + "drop-item.wav");
        break;
    case SoundElfMaleGroan:
        id = m_audio->soundId(path + "male-groan.wav");
        break;
    case SoundElfFemaleGroan:
        id = m_audio->soundId(path + "female-groan.wav");
        break;
    case SoundHumanMaleGroan:
        id = m_audio->soundId(path + "male-groan.wav");
        break;
    case SoundHumanFemaleGroan:
        id = m_audio->soundId(path + "female-groan.wav");
        break;
    case SoundGoblinMaleGroan:
        id = m_audio->soundId(path + "goblin-male-groan.wav");
        break;
    case SoundGoblinFemaleGroan:
        id = m_audio->soundId(path + "goblin-female-groan.wav");
        break;
    case SoundLizardMaleGroan:
        id = m_audio->soundId(path + "lizard-male-groan.wav");
        break;
    case SoundLizardFemaleGroan:
        id = m_audio->soundId(path + "lizard-female-groan.wav");
        break;
    case SoundDwarfMaleGroan:
        id = m_audio->soundId(path + "dwarf-male-groan.wav");
        break;
    case SoundDwarfFemaleGroan:
        id = m_audio->soundId(path + "dwarf-female-groan.wav");
        break;
    case SoundOrcMaleGroan:
        id = m_audio->soundId(path + "orc-male-groan.wav");
        break;
    case SoundOrcFemaleGroan:
        id = m_audio->soundId(path + "orc-female-groan.wav");
        break;
    case SoundUndeadMaleGroan:
        id = m_audio->soundId(path + "undead-male-groan.wav");
        break;
    case SoundUndeadFemaleGroan:
        id = m_audio->soundId(path + "undead-female-groan.wav");
        break;
    case SoundFrogmanMaleGroan:
        id = m_audio->soundId(path + "frogman-male-groan.wav");
        break;
    case SoundFrogmanFemaleGroan:
        id = m_audio->soundId(path + "frogman-female-groan.wav");
        break;
    case SoundMonsterGroan:
        id = m_audio->soundId(path + "monster-groan.wav");
        break;
    case SoundTrollGroan:
        id = m_audio->soundId(path + "troll-groan.wav");
        break;
    case SoundZombieGroan:
        id = m_audio->soundId(path + "zombie-groan.wav");
        break;
    case SoundSlimeGroan:
        id = m_audio->soundId(path + "slime-groan.wav");
        break;
    case SoundMoleGroan:
        id = m_audio->soundId(path + "mole-groan.wav");
        break;
    case SoundMenuOpen2:
        id = m_audio->soundId(path + "menu-open2.wav");
        break;
    case SoundMenuClose2:
        id = m_audio->soundId(path + "menu-close2.wav");
        break;
    case SoundMenuSelect:
        id = m_audio->soundId(path + "menu-select.wav");
        break;
    case SoundMenuTab:
        id = m_audio->soundId(path + "menu-tab.wav");
        break;
    case SoundMenuGrabItem:
        id = m_audio->soundId(path + "menu-grab-item.wav");
        break;
    case SoundMenuDropItem:
        id = m_audio->soundId(path + "menu-drop-item.wav");
        break;
    case SoundCraft:
        id = m_audio->soundId(path + "craft.wav");
        break;
    case SoundCraftProc:
        id = m_audio->soundId(path + "craft-proc.wav");
        break;
    case SoundBird1:
        id = m_audio->soundId(path + "bird1.wav");
        break;
    case SoundBird2:
        id = m_audio->soundId(path + "bird2.wav");
        break;
    case SoundBird3:
        id = m_audio->soundId(path + "bird3.wav");
        break;
    case SoundOwl1:
        id = m_audio->soundId(path + "owl1.wav");
        break;
    case SoundOwl2:
        id = m_audio->soundId(path + "owl2.wav");
        break;
    case SoundCricket1:
        id = m_audio->soundId(path + "cricket1.wav");
        break;
    case SoundCricket2:
        id = m_audio->soundId(path + "cricket2.wav");
        break;
    }
    float pan = (m_worldViewMatrix * plasma::Vector<3, float>(position))[0] * -0.1f;
    if (pan < -1.0f)
        pan = -1.0f;
    else if (pan > 1.0f)
        pan = 1.0f;
    m_audio->play(id, (float)m_options.soundVolume * volume * 0.01f, pan, speed, false);
}

void GameController::playSound(int sound)
{
    playSound(sound, m_cameraPosition, 1.0f, 1.0f);
}

plasma::Texture *GameController::loadTexture(const std::string &path, Database *database)
{
    if (database) {
        BytesIO bytes;
        if (database->read(path, bytes)) {
            bytes.descramble();
            return m_engine->loadImageTexture(widenString(path), reinterpret_cast<unsigned char *>(bytes.data()),
                                              bytes.size());
        }
    }
    return m_engine->loadImageTexture(widenString(path));
}

// Receives exactly length bytes; returns length, or recv's result when the connection fails or
// closes first (the server's receiveAll).
int receiveAll(SOCKET socket, char *buffer, int length, int flags)
{
    int originalLength = length;
    while (length > 0) {
        int received = recv(socket, buffer, length, flags);
        if (received <= 0)
            return received;
        length -= received;
        buffer += received;
    }
    return originalLength;
}

void Chunk::reset()
{
    EnterCriticalSection(&lock);
    position = plasma::Vector<2, int>(-1, -1);
    for (std::list<ChunkBuffer *>::iterator it = chunkBuffers.begin(); it != chunkBuffers.end(); ++it)
        delete *it;
    chunkBuffers.clear();
    for (std::list<ChunkBuffer *>::iterator it = newChunkBuffers.begin(); it != newChunkBuffers.end(); ++it)
        delete *it;
    newChunkBuffers.clear();
    plants.clear();
    newPlants.clear();
    vertexCount = 0;
    lightCount = 0;
    LeaveCriticalSection(&lock);
}

float roundHundredth(float value)
{
    if (value < 0.0f) {
        float result = -roundHundredth(-value);
        if (result == 0.0f)
            result = 0.0f;
        return result;
    }
    return (float)(int)(value * 100.0f + 0.5f) * 0.01f;
}

// Saves a character into slot index of the characters database: the position in the current world
// is remembered with the player data, then a copy of the creature's saved parts (entity data, the
// player data and the inventory) is serialized, compressed and written under the slot number.
void GameController::saveCharacter(int index, Creature *creature)
{
    m_world.enterRegions();
    if (!m_world.getName().empty())
        creature->m_playerData->worldPositions[std::make_pair(m_world.getSeed(), m_world.getName())] =
            creature->m_entityData.position;
    Creature *saved = new Creature(0);
    saved->m_entityData = creature->m_entityData;
    saved->m_ridingStamina = creature->m_ridingStamina;
    saved->unused119c = creature->unused119c;
    saved->m_playerData = new Creature::PlayerData(*creature->m_playerData);
    saved->m_inventory = creature->m_inventory;
    m_world.leaveRegions();
    BytesIO bytes;
    Serializer(&bytes).saveCreature(saved);
    delete saved;
    std::stringstream key;
    key << index;
    bytes.compress();
    EnterCriticalSection(&m_charactersLock);
    m_charactersDatabase.write(key.str(), bytes);
    LeaveCriticalSection(&m_charactersLock);
}

void GameController::saveWorldInfo(int index, WorldInfo *world, bool lockWorld)
{
    if (lockWorld)
        m_world.enterRegions();
    if (world->m_name == m_world.getName() && world->m_seed == m_world.getSeed()) {
        ZoneTile *tile = m_worldMap.getZoneTile((int)m_player->m_entityData.position[0] / BlocksPerZone,
                                                (int)m_player->m_entityData.position[1] / BlocksPerZone);
        if (tile && tile->m_sprite) {
            if (!world->m_preview)
                world->m_preview = new Sprite(m_device, false);
            Sprite *sprite = tile->m_sprite;
            world->m_preview->setVoxels(sprite->sizeX(), sprite->sizeY(), sprite->sizeZ(), sprite->m_voxels, true);
            world->m_preview->updateMesh();
        }
    }
    BytesIO bytes;
    int nameLength = (int)world->m_name.size();
    bytes.write(&nameLength, sizeof(nameLength));
    bytes.write(world->m_name.c_str(), nameLength);
    bytes.write(&world->m_seed, sizeof(world->m_seed));
    std::cout << "wrote " << world->m_seed << std::endl;
    bytes.write(&world->m_exploredArea, sizeof(world->m_exploredArea));
    plasma::Vector<3, int> size(0, 0, 0);
    if (world->m_preview)
        size = world->m_preview->m_size;
    bytes.write(&size, sizeof(size));
    if (size[0] > 0 && size[1] > 0 && size[2] > 0) {
        std::vector<char> voxels(size[0] * size[1] * size[2] * 3);
        memcpy(&voxels[0], world->m_preview->m_voxels, voxels.size());
        std::vector<char> compressed;
        compress(voxels, compressed);
        int length = (int)compressed.size();
        bytes.write(&length, sizeof(length));
        bytes.write(&compressed[0], length);
    }
    if (lockWorld)
        m_world.leaveRegions();
    std::stringstream key;
    key << index;
    m_worldsDatabase.write(key.str(), bytes);
}

Creature *GameController::selectedCharacter()
{
    if (m_selectedCharacter >= 0 && m_selectedCharacter < (int)m_characters.size())
        return m_characters[m_selectedCharacter];
    return 0;
}

// R on a hovered object: the speech bubble already showing the player's words moves on (or reveals
// its text) unless advance is set; otherwise a free bubble (the last one) says "There is nothing
// special." over the player. Doors, windows, chests and the furniture one sits or lies on say
// nothing.
void GameController::useObject(Zone::Object *object, bool advance)
{
    SpeechWidget *widget = 0;
    int count = (int)m_speechWidgets.size();
    for (std::vector<SpeechWidget *>::iterator it = m_speechWidgets.begin(); it != m_speechWidgets.end(); ++it) {
        if ((*it)->m_creature == m_player) {
            widget = *it;
            break;
        }
    }
    if (widget && !advance) {
        if (widget->isDialogFinished()) {
            widget->m_dialogIndex++;
            widget->m_readTime = 0;
            widget->m_textUncovered = 0;
        } else {
            widget->m_readTime = widget->getDialogDuration();
        }
        return;
    }
    if (!widget) {
        for (int i = 0; i < count; i++) {
            if (!m_speechWidgets[i]->m_creature)
                widget = m_speechWidgets[i];
        }
        if (!widget)
            return;
    }
    int type = object->type;
    if (type == StaticObjectDoor || type == StaticObjectBigDoor || type == StaticObjectWindow ||
        type == StaticObjectChest || type == StaticObjectBench || type == StaticObjectStool ||
        type == StaticObjectBed || type == StaticObjectSleepingMat)
        return;
    widget->getNode()->setVisible(true);
    widget->m_creature = m_player;
    widget->m_anchorPosition = m_player->m_entityData.position;
    widget->m_dialogs.clear();
    Dialog dialog;
    dialog.write(L"There is nothing special.", plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    widget->m_dialogs.push_back(std::move(dialog));
    widget->m_readTime = 0;
    widget->m_textUncovered = 0;
    widget->m_dialogIndex = 0;
    widget->m_choices.clear();
    widget->m_hoveredChoice = 0;
}

// Talks with a creature in one of the speech widgets: the one already showing it, or else the last
// free one. Without restart a showing conversation moves on to its next page when the page has
// been read, or waits the page's reading time. A creature with bit 7 of its appearance flags shows
// the player the region's places within two levels of the player's on the map; others start their
// dialogs: an innkeeper's (class 0x84), or a quest about its zone (none if the zone's level is not
// within two of the player's), whose zone the map discovers.
void GameController::talkTo(Creature *creature, bool restart)
{
    if (!creature)
        return;
    if (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagHasDialog)) {
        SpeechWidget *widget = 0;
        for (std::vector<SpeechWidget *>::iterator it = m_speechWidgets.begin(); it != m_speechWidgets.end(); ++it) {
            if ((*it)->m_creature == creature) {
                widget = *it;
                break;
            }
        }
        if (widget) {
            if (!restart) {
                if (widget->isDialogFinished()) {
                    widget->m_dialogIndex++;
                    widget->m_readTime = 0;
                    widget->m_textUncovered = 0;
                } else {
                    widget->m_readTime = widget->getDialogDuration();
                }
                return;
            }
        } else {
            for (int i = 0; i < (int)m_speechWidgets.size(); i++) {
                if (m_speechWidgets[i]->m_creature == 0)
                    widget = m_speechWidgets[i];
            }
            if (!widget)
                return;
        }
        std::vector<Dialog> dialogs;
        Region *region = m_world.getRegion(creature->m_entityData.spawnSlot[0] / ZonesPerRegion,
                                           creature->m_entityData.spawnSlot[1] / ZonesPerRegion);
        if (!region)
            return;
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                Region::Record &record = region->m_records[i * 8 + j];
                if (record.kind != Region::Record::SceneryNone && record.level <= m_player->m_entityData.level + 2)
                    m_worldMap.discover((int)record.center[0] / BlocksPerZone, (int)record.center[1] / BlocksPerZone);
            }
        }
        return;
    }

    SpeechWidget *widget = 0;
    for (std::vector<SpeechWidget *>::iterator it = m_speechWidgets.begin(); it != m_speechWidgets.end(); ++it) {
        if ((*it)->m_creature == creature) {
            widget = *it;
            break;
        }
    }
    if (widget) {
        if (!restart) {
            if (widget->isDialogFinished()) {
                widget->m_dialogIndex++;
                widget->m_readTime = 0;
                widget->m_textUncovered = 0;
            } else {
                widget->m_readTime = widget->getDialogDuration();
            }
            return;
        }
    } else {
        for (int i = 0; i < (int)m_speechWidgets.size(); i++) {
            if (m_speechWidgets[i]->m_creature == 0)
                widget = m_speechWidgets[i];
        }
        if (!widget)
            return;
    }
    std::vector<Dialog> dialogs;
    plasma::Vector<3, int> location = creature->m_entityData.questZone;
    Region::Record *record =
        m_world.getRecord(location[0] / ZonesPerSceneryRecord, location[1] / ZonesPerSceneryRecord);
    if (record && (record->level > m_player->m_entityData.level + QuestLevelRange ||
                   record->level < m_player->m_entityData.level - QuestLevelRange)) {
        location[0] = -1;
        location[1] = -1;
    }
    location[2] = (int)creature->m_id;
    if (creature->m_entityData.classType == ClassInnkeeper) {
        Speech::Context context;
        m_world.m_speech.generateDialogs(L"innkeeper", &context, dialogs);
    } else {
        m_world.m_speech.generateQuest(&m_world, creature->m_entityData.speechExpression, location, dialogs);
        m_worldMap.discover(location[0], location[1]);
        ZoneTile *tile = m_worldMap.getZoneTile(location[0], location[1]);
        if (tile) {
            Region::Cell *cell = m_world.getCell(location[0], location[1]);
            if (cell)
                tile->m_cell = *cell;
        }
    }
    if (!dialogs.empty() && !dialogs.front().phrases.empty()) {
        widget->getNode()->setVisible(true);
        widget->m_creature = creature;
        widget->m_anchorPosition = creature->m_drawnPosition;
        widget->m_dialogs = dialogs;
        widget->m_readTime = 0;
        widget->m_textUncovered = 0;
        widget->m_dialogIndex = 0;
        widget->m_choices.clear();
        widget->m_hoveredChoice = 0;
    }
}

void GameController::openInteraction()
{
    std::map<long long, Creature *>::iterator found = m_world.m_creatures.find(m_interactionCreatureId);
    Creature *creature = found == m_world.m_creatures.end() ? 0 : found->second;
    m_tradeCreature = creature;
    if (!creature) {
        Creature *nearby = closestPet();
        if (nearby && nearby->m_entityData.ownerId == -1)
            m_player->m_entityData.ownerId = nearby->m_id;
        return;
    }
    if (creature->m_entityData.classType == ClassAnalyzer) {
        m_enchantPanel->setVisible(true);
        m_enchantWidget->SetInventoryItem(-1, -1);
        m_enchantWidget->m_canIdentify = 1;
        return;
    }
    if (creature->m_entityData.classType == ClassItemVendor || creature->m_entityData.classType == ClassWeaponVendor ||
        creature->m_entityData.classType == ClassArmorVendor) {
        updateVendor();
        m_tradePanel->setVisible(true);
        m_inventoryNode->setVisible(true);
        m_tradeWidget->m_tabIndex = VendorTabStock;
        m_tradeInventory = creature->m_inventory;
    }
    if (creature->m_entityData.classType == ClassAdapter)
        m_adaptionPanel->setVisible(true);
}

// The shape as a smooth mesh, or null when it is none.
static plasma::SmoothMeshShape *smoothMeshShape(plasma::Shape *shape)
{
    if (shape && shape->getType() == plasma::SHAPE_SMOOTH_MESH)
        return (plasma::SmoothMeshShape *)shape;
    return 0;
}

// The shape as a text shape, or null when it is none.
static plasma::TextShape *textShape(plasma::Shape *shape)
{
    if (shape && shape->getType() == plasma::SHAPE_TEXT)
        return (plasma::TextShape *)shape;
    return 0;
}

void GameController::toggleCraftingWidget()
{
    m_craftingNode->setVisible(!m_craftingNode->isVisible());
    updateFormulas();
}

void GameController::toggleInventoryWidget()
{
    m_inventoryNode->setVisible(!m_inventoryNode->isVisible());
    m_inventoryWidget->updateRows();
    m_inventoryWidget->updateScrollButton();
    if (m_inventoryNode->isVisible())
        m_systemPanel->setVisible(false);
}

void GameController::toggleSkillWidget()
{
    m_skillsOpen = !m_skillsOpen;
    if (m_skillsOpen) {
        for (int i = 0; i < SkillCount; ++i)
            m_skillWidget->m_skillLevels[i] = m_player->m_entityData.skillLevels[i];
        m_skillWidget->m_specialization = m_player->m_entityData.specialization;
        m_craftingNode->setVisible(false);
        m_blueprintNode->setVisible(false);
    }
}

void GameController::toggleSystemWidget()
{
    m_systemPanel->setVisible(!m_systemPanel->isVisible());
    if (m_systemPanel->isVisible())
        m_inventoryNode->setVisible(false);
}

// Updates the start menu, world, interface, player input, camera and streaming requests in that order. The region lock stays held until the end of the frame.
void GameController::update(int milliseconds)
// The interface is laid out in pixels: the bars', tags' and panels' translations (x, y; insets
// from the window's edges or centre), the skill panel's grid, the previews' offsets from the
// pointer. Colours are RGBA, 0..1 (0..255 for the chat's Vector<3, unsigned char> colours). The
// floating texts (TextFX) have a colour, a duration in milliseconds, an offset and a size in
// pixels and a rise in pixels over their duration; an outline an eighth of the size in a tenth of
// the colour. The temperature shown is -20 + 60 times the world's blended temperature (degrees).
// The lights have a radius in blocks, an RGB colour and a duration.
{
    // A lost connection ends the session. The start menu's panel moves between the title screen and
    // the game screen.
    if (m_connected && !m_socket)
        disconnect();
    if (m_startScreen->isVisible()) {
        if (m_optionsPanel->m_parent != m_startScreen)
            m_optionsPanel->setParent(m_startScreen);
        if (m_cubeWorldLogo)
            m_cubeWorldLogo->setVisible(!m_optionsPanel->isVisible());
        m_startMenuNode->setVisible(!m_optionsPanel->isVisible());
    } else if (m_optionsPanel->m_parent != m_gameScreen) {
        m_optionsPanel->setParent(m_gameScreen);
    }

    // The radius around the camera within which every chunk of the view square has its mesh: the
    // nearest chunk without one (or holding another zone) limits it. It is blended, less the 22.4
    // blocks of fog, and kept tiny while another world than the selected one is loaded.
    m_world.enterRegions();
    float radiusSquared = m_viewRadius * m_viewRadius;
    for (int x = m_chunkOrigin[0]; x < m_chunkOrigin[0] + m_viewDistance; x++) {
        for (int y = m_chunkOrigin[1]; y < m_chunkOrigin[1] + m_viewDistance; y++) {
            if (x < 0 || y < 0 || x >= ChunksPerWorld || y >= ChunksPerWorld)
                continue;
            Chunk *chunk = &m_chunks[(y % m_viewDistance) * m_viewDistance + x % m_viewDistance];
            if (chunk->chunkBuffers.size() == 0 || chunk->position != plasma::Vector<2, int>(x, y)) {
                plasma::Vector<3, Fixed> center((Fixed(x) - Fixed(-0.5)) * Fixed(BlocksPerChunk),
                                                (Fixed(y) - Fixed(-0.5)) * Fixed(BlocksPerChunk), m_cameraPosition[2]);
                plasma::Vector<3, float> offset(center - m_cameraPosition);
                float distanceSquared = offset[0] * offset[0] + offset[1] * offset[1];
                if (distanceSquared < radiusSquared)
                    radiusSquared = distanceSquared;
            }
        }
    }
    float radius = (float)sqrt(radiusSquared) - 22.4f;
    float factor = blendFactor(milliseconds, 0.001f);
    m_loadedMeshRadius = (1.0f - factor) * m_loadedMeshRadius + factor * radius;
    if (m_loadedMeshRadius < 0.0001f || m_seed != m_world.getSeed() || m_worldName != m_world.getName())
        m_loadedMeshRadius = 0.0001f;

    // The start menu's screens. "Please wait..." covers a world that is not loaded around the camera
    // yet.
    m_backButton->setVisible(m_characterListScreen->isVisible() || m_characterCreationScreen->isVisible() ||
                             m_worldListScreen->isVisible() || m_worldCreationScreen->isVisible() ||
                             m_connectScreen->isVisible());
    m_waitNode->setVisible(
        !((!(m_loadedMeshRadius < PlayableMeshRadius) || m_startScreen->isVisible() ||
           m_characterListScreen->isVisible() || m_connectScreen->isVisible() || m_worldListScreen->isVisible()) &&
          m_seed == m_world.getSeed() && m_worldName == m_world.getName()));
    m_worldListButton->setVisible(m_worldListScreen->isVisible());
    m_connectScreenButton->setVisible(m_worldListScreen->isVisible());
    m_connectButton->setVisible(m_connectScreen->isVisible());
    if (m_chatWidget->m_typeboxActive)
        m_chatWidget->m_changed = true;
    m_gameScreen->setVisible(!m_characterCreationScreen->isVisible() && !m_characterListScreen->isVisible() &&
                             !m_connectScreen->isVisible() && !m_waitNode->isVisible() &&
                             !m_worldCreationScreen->isVisible());
    m_nameError->setVisible(false);

    // A new character's name: 2 to 15 characters, and none of the saved characters' names.
    if (m_characterCreationScreen->isVisible()) {
        std::wstring name = m_characterNameNode->getText(L"edit", true);
        // The code walks the name's characters without effect.
        for (std::wstring::iterator it = name.begin(); it != name.end(); ++it) {
        }
        bool valid;
        std::vector<Creature *>::iterator it;
        for (it = m_characters.begin(); it != m_characters.end(); ++it) {
            if (widenString(std::string((*it)->m_entityData.name)) == name)
                break;
        }
        if (it != m_characters.end()) {
            m_nameError->setVisible(true);
            m_nameError->setText(L"There is already a character with that name.");
            valid = false;
        } else {
            valid = name.size() >= 2 && name.size() <= 15;
        }
        m_createCharacterButton->setVisible(valid);
    }

    // A new world: a name of two characters or more that no saved world has, and a seed of digits.
    if (m_worldCreationScreen->isVisible()) {
        std::wstring name = m_worldNameNode->getText(L"edit", true);
        // The code walks the name's characters without effect.
        for (std::wstring::iterator it = name.begin(); it != name.end(); ++it) {
        }
        std::wstring seed = m_worldSeedNode->getText(L"edit", true);
        bool badSeed = false;
        for (std::wstring::iterator it = seed.begin(); it != seed.end(); ++it) {
            if (*it < L'0' || *it > L'9')
                badSeed = true;
        }
        bool valid;
        std::vector<WorldInfo *>::iterator it;
        for (it = m_worlds.begin(); it != m_worlds.end(); ++it) {
            if (widenString((*it)->m_name) == name)
                break;
        }
        if (it != m_worlds.end()) {
            m_nameError->setVisible(true);
            m_nameError->setText(L"There is already a world with that name.");
            valid = false;
        } else {
            valid = !badSeed && name.size() > 1 && name.size() != 0 && seed.size() != 0;
        }
        m_createWorldButton->setVisible(valid);
    }

    // Behind the title and connect screens, and the character and world lists, the camera circles the
    // scene.
    if ((m_startScreen->isVisible() || m_connectScreen->isVisible()) && !m_waitNode->isVisible()) {
        m_cameraDistance = MenuCameraDistance;
        m_targetCameraRotation[0] = MenuCameraPitch;
        m_targetCameraRotation[2] += milliseconds * 0.005f;
    }

    // The saved-character rows are centered and blended into position. They scroll by m_characterListScrollSpeed; the row under the pointer is selected and receives its Select or Create, Menu and Delete buttons.
    if (m_characterListScreen->isVisible() && !m_waitNode->isVisible()) {
        m_cameraDistance = MenuCameraDistance;
        m_targetCameraRotation[0] = MenuCameraPitch;
        m_targetCameraRotation[2] += milliseconds * 0.005f;
        if (!m_engine->isButtonDown(plasma::MOUSE_BUTTON_LEFT) && !m_characterRows.empty()) {
            // The list's layout, in pixels: kept 10 pixels inside the window (20 for both edges), its row
            // buttons 20 pixels inside the row.
            float top = m_characterRows.front()->widget()->viewY();
            float minimumTop = 10.0f;
            float maximumBottom = (float)(m_windowHeight - 10) - m_characterRows.back()->widget()->getViewHeight();
            float height = m_characterRows.back()->widget()->viewY() +
                           m_characterRows.back()->widget()->getViewHeight() -
                           m_characterRows.front()->widget()->viewY();
            if ((int)height < m_windowHeight - 20) {
                minimumTop = (float)(m_windowHeight / 2 - (int)height / 2);
                maximumBottom =
                    (float)((int)height / 2 + m_windowHeight / 2) - m_characterRows.back()->widget()->getViewHeight();
            }
            if (top > minimumTop) {
                blend(top, minimumTop, milliseconds, 0.01f);
                float shift = top - m_characterRows.front()->widget()->viewY();
                for (std::vector<plasma::Node *>::iterator it = m_characterRows.begin(); it != m_characterRows.end();
                     it++) {
                    plasma::Widget *widget = (*it)->widget();
                    widget->moveTo((float)(m_windowWidth / 2) - (*it)->widget()->viewWidth() * 0.5f,
                                   (*it)->widget()->viewY() + shift, true);
                }
            }
            float bottom = m_characterRows.back()->widget()->viewY();
            if (minimumTop > m_characterRows.front()->widget()->viewY() && maximumBottom > bottom) {
                blend(bottom, maximumBottom, milliseconds, 0.01f);
                float shift = bottom - m_characterRows.back()->widget()->viewY();
                for (std::vector<plasma::Node *>::iterator it = m_characterRows.begin(); it != m_characterRows.end();
                     it++) {
                    plasma::Widget *widget = (*it)->widget();
                    widget->moveTo((float)(m_windowWidth / 2) - (*it)->widget()->viewWidth() * 0.5f,
                                   (*it)->widget()->viewY() + shift, true);
                }
            }
            int index = 0;
            for (std::vector<plasma::Node *>::iterator it = m_characterRows.begin(); it != m_characterRows.end();
                 it++) {
                plasma::Widget *widget = (*it)->widget();
                widget->moveTo((float)(m_windowWidth / 2) - (*it)->widget()->viewWidth() * 0.5f,
                               (*it)->widget()->viewY() + m_characterListScrollSpeed * 0.001f * milliseconds, true);
                if ((*it)->widget()->isPointerOver()) {
                    if (index != m_selectedCharacter)
                        m_deleteCharacterButton->setVisible(false);
                    m_selectedCharacter = index;
                }
                index++;
            }
        }
        if (!m_engine->isButtonDown(plasma::MOUSE_BUTTON_LEFT) && m_selectedCharacter >= 0 &&
            m_selectedCharacter < (int)m_characterRows.size()) {
            float bottom = m_characterRows[m_selectedCharacter]->widget()->viewY() +
                           m_characterRows[m_selectedCharacter]->widget()->getViewHeight();
            float right = m_characterRows[m_selectedCharacter]->widget()->viewX() +
                          m_characterRows[m_selectedCharacter]->widget()->viewWidth();
            m_selectCharacterButton->widget()->moveTo(
                right - m_selectCharacterButton->widget()->viewWidth() - 20.0f,
                bottom - m_selectCharacterButton->widget()->getViewHeight() - 20.0f, true);
            m_selectCharacterButton->setVisible(true);
            right = m_characterRows[m_selectedCharacter]->widget()->viewX() +
                    m_characterRows[m_selectedCharacter]->widget()->viewWidth();
            m_characterMenuButton->widget()->moveTo(right - m_characterMenuButton->widget()->viewWidth() - 20.0f,
                                                    m_characterRows[m_selectedCharacter]->widget()->viewY() + 20.0f,
                                                    true);
            m_characterMenuButton->setVisible(true);
            m_deleteCharacterButton->widget()->moveTo(m_characterRows[m_selectedCharacter]->widget()->viewX() +
                                                          m_characterRows[m_selectedCharacter]->widget()->viewWidth() +
                                                          20.0f,
                                                      m_characterRows[m_selectedCharacter]->widget()->viewY(), true);
        } else {
            m_selectCharacterButton->setVisible(false);
            m_characterMenuButton->setVisible(false);
            m_deleteCharacterButton->setVisible(false);
        }
        // The row after the saved characters creates one.
        if (m_selectedCharacter >= (int)m_characters.size()) {
            m_deleteCharacterButton->setVisible(false);
            m_characterMenuButton->setVisible(false);
            m_selectCharacterButton->setText(L"Create");
        } else {
            m_selectCharacterButton->setText(L"Select");
        }
        blend(m_characterListScrollSpeed, 0.0f, milliseconds, 0.005f);
    } else {
        m_characterListScrollSpeed = 0;
    }

    // The world list, laid out the same way; its rows map to world indices through m_worldRowIndices
    // and back through m_worldRowsByIndex, and the selected world gets m_selectWorldButton (Select or
    // Create), m_worldMenuButton and m_deleteWorldButton.
    if (m_worldListScreen->isVisible() && !m_waitNode->isVisible()) {
        m_cameraDistance = MenuCameraDistance;
        m_targetCameraRotation[0] = MenuCameraPitch;
        m_targetCameraRotation[2] += milliseconds * 0.005f;
        if (!m_engine->isButtonDown(plasma::MOUSE_BUTTON_LEFT) && !m_worldRows.empty()) {
            float top = m_worldRows.front()->widget()->viewY();
            float minimumTop = 10.0f;
            float maximumBottom = (float)(m_windowHeight - 10) - m_worldRows.back()->widget()->getViewHeight();
            float height = m_worldRows.back()->widget()->viewY() + m_worldRows.back()->widget()->getViewHeight() -
                           m_worldRows.front()->widget()->viewY();
            if ((int)height < m_windowHeight - 20) {
                minimumTop = (float)(m_windowHeight / 2 - (int)height / 2);
                maximumBottom =
                    (float)((int)height / 2 + m_windowHeight / 2) - m_worldRows.back()->widget()->getViewHeight();
            }
            if (top > minimumTop) {
                blend(top, minimumTop, milliseconds, 0.01f);
                float shift = top - m_worldRows.front()->widget()->viewY();
                for (std::vector<plasma::Node *>::iterator it = m_worldRows.begin(); it != m_worldRows.end(); it++) {
                    plasma::Widget *widget = (*it)->widget();
                    widget->moveTo((float)(m_windowWidth / 2) - (*it)->widget()->viewWidth() * 0.5f,
                                   (*it)->widget()->viewY() + shift, true);
                }
            }
            float bottom = m_worldRows.back()->widget()->viewY();
            if (minimumTop > m_worldRows.front()->widget()->viewY() && maximumBottom > bottom) {
                blend(bottom, maximumBottom, milliseconds, 0.01f);
                float shift = bottom - m_worldRows.back()->widget()->viewY();
                for (std::vector<plasma::Node *>::iterator it = m_worldRows.begin(); it != m_worldRows.end(); it++) {
                    plasma::Widget *widget = (*it)->widget();
                    widget->moveTo((float)(m_windowWidth / 2) - (*it)->widget()->viewWidth() * 0.5f,
                                   (*it)->widget()->viewY() + shift, true);
                }
            }
            for (std::vector<plasma::Node *>::iterator it = m_worldRows.begin(); it != m_worldRows.end(); it++) {
                plasma::Widget *widget = (*it)->widget();
                widget->moveTo((float)(m_windowWidth / 2) - (*it)->widget()->viewWidth() * 0.5f,
                               (*it)->widget()->viewY() + m_worldListScrollSpeed * 0.001f * milliseconds, true);
                if ((*it)->widget()->isPointerOver()) {
                    int index = m_worldRowIndices[*it];
                    if (index != m_selectedWorld)
                        m_deleteWorldButton->setVisible(false);
                    m_selectedWorld = index;
                }
            }
        }
        if (!m_engine->isButtonDown(plasma::MOUSE_BUTTON_LEFT) && m_worldRowsByIndex[m_selectedWorld] != 0) {
            float bottom = m_worldRowsByIndex[m_selectedWorld]->widget()->viewY() +
                           m_worldRowsByIndex[m_selectedWorld]->widget()->getViewHeight();
            float right = m_worldRowsByIndex[m_selectedWorld]->widget()->viewX() +
                          m_worldRowsByIndex[m_selectedWorld]->widget()->viewWidth();
            m_selectWorldButton->widget()->moveTo(right - m_selectWorldButton->widget()->viewWidth() - 20.0f,
                                                  bottom - m_selectWorldButton->widget()->getViewHeight() - 20.0f,
                                                  true);
            m_selectWorldButton->setVisible(true);
            right = m_worldRowsByIndex[m_selectedWorld]->widget()->viewX() +
                    m_worldRowsByIndex[m_selectedWorld]->widget()->viewWidth();
            m_worldMenuButton->widget()->moveTo(right - m_worldMenuButton->widget()->viewWidth() - 20.0f,
                                                m_worldRowsByIndex[m_selectedWorld]->widget()->viewY() + 20.0f, true);
            m_worldMenuButton->setVisible(true);
            m_deleteWorldButton->widget()->moveTo(m_worldRowsByIndex[m_selectedWorld]->widget()->viewX() +
                                                      m_worldRowsByIndex[m_selectedWorld]->widget()->viewWidth() +
                                                      20.0f,
                                                  m_worldRowsByIndex[m_selectedWorld]->widget()->viewY(), true);
        } else {
            m_selectWorldButton->setVisible(false);
            m_worldMenuButton->setVisible(false);
            m_deleteWorldButton->setVisible(false);
        }
        if (m_selectedWorld >= (int)m_worlds.size()) {
            m_deleteWorldButton->setVisible(false);
            m_worldMenuButton->setVisible(false);
            m_selectWorldButton->setText(L"Create");
        } else {
            m_selectWorldButton->setText(L"Select");
        }
        blend(m_worldListScrollSpeed, 0.0f, milliseconds, 0.005f);
    } else {
        m_worldListScrollSpeed = 0;
    }

    // A sound when the pointer appears or goes.
    static bool cursorWasVisible;
    bool cursorVisible;
    if (isCursorVisible() || m_ringMenuOpen) {
        cursorVisible = true;
        if (!cursorWasVisible)
            playSound(SoundMenuOpen2, m_cameraPosition, 1.0f, 1.0f);
    } else {
        cursorVisible = false;
        if (cursorWasVisible)
            playSound(SoundMenuClose2, m_cameraPosition, 1.0f, 1.0f);
    }
    cursorWasVisible = cursorVisible;

    // The audio engine's housekeeping, and every ten seconds the font engine drops the glyphs it has
    // not drawn since.
    int now = timeGetTime();
    Sprite::s_frame = now;
    m_audio->cleanUp();
    m_audio->update();
    static int lastGlyphCollection;
    if (now - lastGlyphCollection > 10000) {
        m_engine->getFontEngine()->collectUnusedGlyphs();
        lastGlyphCollection = now;
    }

    // The pointer's label: the count of the stack it carries, or the name (and level) of the upgrade
    // item under it.
    m_cursorNode->setText(L"");
    if (isCursorVisible()) {
        if (m_player->m_inventory.m_cursor.quantity != 0) {
            if (m_player->m_inventory.m_cursor.item.isStackable()) {
                std::wstringstream text;
                text << m_player->m_inventory.m_cursor.quantity;
                m_cursorNode->setText(text.str());
            } else {
                m_cursorNode->setText(L"");
            }
        }
        if (m_voxelPanel->isVisible() && m_voxelWidget->m_selectedUpgradeItem.category != ItemCategoryNone) {
            std::wstringstream text;
            text << m_world.createItemName(m_voxelWidget->m_selectedUpgradeItem);
            if (m_voxelWidget->m_selectedUpgradeItem.scalesWithLevel())
                text << " +" << m_voxelWidget->m_selectedUpgradeItem.power();
            m_cursorNode->setText(text.str());
        }
    }

    // The map: "Teleport" over the marked place, and the middle button drags the view (turned with the
    // camera, scaled by the zoom).
    if (m_mapOpen) {
        if (m_worldMap.m_canTeleport && m_worldMap.m_selectedZone != plasma::Vector<2, int>(-1, -1) &&
            m_worldMap.m_selectedZone == m_worldMap.m_hoveredZone)
            m_cursorNode->setText(L"Teleport");
        if (m_inputs[InputMouseMiddle]) {
            plasma::Matrix<float> turn;
            turn.identity();
            turn.rotateZ(m_cameraRotation[2]);
            EnterCriticalSection(&m_streamingLock);
            plasma::Vector<2, float> drag = m_engine->getPointerDelta();
            m_mapOffset +=
                turn.transformDirection(MapDragSpeed / m_mapZoom * plasma::Vector<3, float>(drag[0], -drag[1], 0.0f));
            LeaveCriticalSection(&m_streamingLock);
        }
    } else {
        EnterCriticalSection(&m_streamingLock);
        m_mapOffset = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        LeaveCriticalSection(&m_streamingLock);
    }

    // The skill bar. The crafting screen closes the skill screen.
    if (m_craftingNode->isVisible())
        m_skillsOpen = false;
    for (std::vector<plasma::Node *>::iterator it = m_skillButtons.begin(); it != m_skillButtons.end(); it++)
        (*it)->setVisible(false);
    // The two weapon skills: the wands, bracelets and staffs give two by specialization; other weapons
    // the next attack and a skill of the weapon kind. Then three of the class.
    m_barAbilities.clear();
    Creature::EntityData &player = m_player->m_entityData;
    if (player.equipment.weaponRight.id == WeaponStaff) {
        if (player.specialization == SpecializationWater) {
            m_barAbilities.push_back(AbilityWaterSwirl);
            m_barAbilities.push_back(AbilityWaterVortex);
        } else {
            m_barAbilities.push_back(AbilityFireSwirl);
            m_barAbilities.push_back(AbilityFireVortex);
        }
    } else if (player.equipment.weaponRight.id == WeaponBracelet) {
        if (player.specialization == SpecializationWater) {
            m_barAbilities.push_back(AbilityBraceletWaterDrop);
            m_barAbilities.push_back(AbilityWaterSplash);
        } else {
            m_barAbilities.push_back(AbilityBraceletFirebolt);
            m_barAbilities.push_back(AbilityFireball);
        }
    } else if (player.equipment.weaponRight.id == WeaponWand) {
        if (player.specialization == SpecializationWater) {
            m_barAbilities.push_back(AbilityWandWaterDrop);
            m_barAbilities.push_back(AbilityWaterSalvo);
        } else {
            m_barAbilities.push_back(AbilityWandFirebolt);
            m_barAbilities.push_back(AbilityFireSalvo);
        }
    } else {
        m_barAbilities.push_back(m_player->nextAttackSkill());
        if (player.equipment.weaponRight.id == WeaponLongsword)
            m_barAbilities.push_back(AbilityPerforate);
        else if (player.equipment.weaponRight.id == WeaponDagger)
            m_barAbilities.push_back(AbilityAmbush);
        else if (player.equipment.weaponRight.id == WeaponFist)
            m_barAbilities.push_back(AbilityKick);
        else if (player.equipment.weaponRight.id == WeaponBow || player.equipment.weaponLeft.id == WeaponBow)
            m_barAbilities.push_back(AbilitySalvo);
        else if (player.equipment.weaponRight.id == WeaponCrossbow || player.equipment.weaponLeft.id == WeaponCrossbow)
            m_barAbilities.push_back(AbilityChargedShot);
        else if (player.equipment.weaponRight.id == WeaponBoomerang)
            m_barAbilities.push_back(AbilityChargeBoomerang);
        else if (m_player->hasTwoHandedWeapon())
            m_barAbilities.push_back(AbilityCrush);
        else if (m_player->m_entityData.equipment.weaponLeft.id != WeaponShield)
            m_barAbilities.push_back(AbilitySwirl);
        else
            m_barAbilities.push_back(AbilityBlock);
    }
    if (player.classType == ClassMage) {
        m_barAbilities.push_back(player.specialization == SpecializationWater ? AbilityHealingStream
                                                                              : AbilityFireExplosion);
        m_barAbilities.push_back(AbilityManaShield);
        m_barAbilities.push_back(AbilityTeleport);
    } else if (player.classType == ClassRogue) {
        m_barAbilities.push_back(AbilityIntercept);
        m_barAbilities.push_back(AbilitySneak);
        m_barAbilities.push_back(m_player->m_entityData.specialization == SpecializationAssassin ? AbilityCamouflage
                                                                                                 : AbilityShuriken);
    } else if (player.classType == ClassRanger) {
        m_barAbilities.push_back(AbilityRangerKick);
        m_barAbilities.push_back(AbilityRetreat);
        m_barAbilities.push_back(m_player->m_entityData.specialization == SpecializationScout ? AbilitySwiftness
                                                                                              : AbilityAim);
    } else if (player.classType == ClassWarrior) {
        m_barAbilities.push_back(AbilitySmash);
        m_barAbilities.push_back(AbilityCyclone);
        m_barAbilities.push_back(m_player->m_entityData.specialization == SpecializationBerserker ? AbilityWarFrenzy
                                                                                                  : AbilityBulwark);
    }
    // The class skills without a skill point stay empty.
    for (int i = AbilityBarKey1; i < (int)m_barAbilities.size(); i++) {
        if (i + BarSkillOffset <= SkillClassAbilityFifth && m_player->m_entityData.skillLevels[i + BarSkillOffset] <= 0)
            m_barAbilities[i] = AbilityNone;
    }

    // The ability buttons: the cooldown cover shrinks with the time left, the icon is the skill's,
    // bright when it can be used.
    for (int i = 0; i < (int)m_abilityButtons.size(); i++) {
        bool hasIcon = false;
        if (i < (int)m_barAbilities.size()) {
            if (i < (int)m_cooldowns.size()) {
                if (m_player->m_skillCooldowns[m_barAbilities[i]] != 0 && m_player->cooldown(m_barAbilities[i]) != 0) {
                    m_cooldowns[i]->setVisible(true);
                    m_cooldowns[i]->transformation()->m_deformation.value().identity();
                    float left;
                    if ((float)m_player->m_skillCooldowns[m_barAbilities[i]] /
                            (float)m_player->cooldown(m_barAbilities[i]) <
                        0.0f)
                        left = 0.0f;
                    else
                        left = (float)m_player->m_skillCooldowns[m_barAbilities[i]] /
                               (float)m_player->cooldown(m_barAbilities[i]);
                    m_cooldowns[i]->transformation()->m_deformation.value().scale(1.0f, left);
                    m_cooldowns[i]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
                } else {
                    m_cooldowns[i]->setVisible(false);
                }
            }
            if (m_abilityIcons[m_barAbilities[i]]) {
                int texture = m_abilityIcons[m_barAbilities[i]]->id();
                smoothMeshShape(m_abilityButtons[i]->getShape())->m_texture = texture;
                float saturation = m_player->canUseSkill(m_barAbilities[i]) ? 1.0f : 0.0f;
                smoothMeshShape(m_abilityButtons[i]->getShape())->m_textureSaturation = saturation;
                hasIcon = true;
            }
        }
        if (!hasIcon) {
            int texture = m_abilityIcons[AbilityNone]->id();
            smoothMeshShape(m_abilityButtons[i]->getShape())->m_texture = texture;
        }
        smoothMeshShape(m_abilityButtons[i]->getShape())->updateDrawingTextures();
    }

    // Slow motion: a quarter of the time, at least a millisecond.
    if (m_slowMotion) {
        milliseconds /= 4;
        if (milliseconds < 1) {
            milliseconds = 1;
            Sleep(SlowMotionSleep);
        }
    }

    // The selected world's saved entry keeps the number of zone tiles discovered.
    if (!m_world.getName().empty() && m_selectedWorld >= 0 && m_selectedWorld < (int)m_worlds.size() &&
        m_worlds[m_selectedWorld]->m_seed == m_world.getSeed() &&
        m_worlds[m_selectedWorld]->m_name == m_world.getName())
        m_worlds[m_selectedWorld]->m_exploredArea = m_worldMap.getDiscovered();

    // The creatures that hold aggro on the player (a boss, or one with AppearanceFlagTinted, makes it
    // a boss fight) set the combat music's volume.
    bool bossFight = false;
    bool inCombat = false;
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         it++) {
        Creature *creature = it->second;
        if (creature && creature->m_entityData.hostilityType != HostilityAttackableObject) {
            for (std::map<long long, float>::iterator aggro = creature->m_aggro.begin();
                 aggro != creature->m_aggro.end(); aggro++) {
                if (aggro->first == m_player->m_id) {
                    if (creature->isBoss() ||
                        (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagTinted)))
                        bossFight = true;
                    inCombat = true;
                }
            }
        }
    }
    static bool playerDead;
    if (m_player->m_entityData.hp <= 0.0f) {
        if (!playerDead)
            playerDead = true;
    } else {
        playerDead = false;
    }
    static bool bossMusic;
    static float combatVolume;
    if (inCombat)
        blend(combatVolume, m_options.musicVolume * 0.01f, milliseconds, 0.001f);
    else
        blend(combatVolume, 0.0f, milliseconds, 0.00005f);
    bossMusic = bossFight;
    m_audio->setMusicVolume(m_options.musicVolume * 0.01f, combatVolume);

    // Where the player's head shows on the screen (the dialogs are placed by it): projected, kept 100
    // pixels from the top and 250 from the bottom; the screen's centre while behind the camera.
    plasma::Vector<3, float> headOnScreen(
        m_player->m_entityData.position +
        plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(-m_player->m_stepOffset)));
    headOnScreen = m_viewMatrix * headOnScreen;
    if (headOnScreen[2] > 0.0f) {
        m_projectionMatrix.transform(headOnScreen);
        headOnScreen *= (0.5f * plasma::Vector<3, float>((float)m_engine->viewportWidth(),
                                                         (float)-m_engine->viewportHeight(), 1.0f));
        headOnScreen +=
            0.5f * plasma::Vector<3, float>((float)m_engine->viewportWidth(), (float)m_engine->viewportHeight(), 0.0f);
        if (headOnScreen[1] < 100.0f)
            headOnScreen[1] = 100.0f;
        if (headOnScreen[1] > (float)(m_windowHeight - 250))
            headOnScreen[1] = (float)(m_windowHeight - 250);
    } else {
        headOnScreen = plasma::Vector<3, float>((float)(m_windowWidth / 2), (float)(m_windowHeight / 2), 0.0f);
    }

    // The blueprint shows with the crafting screen (the crafting widget's selected cell is read, and
    // not used).
    if (m_blueprintNode && m_craftingNode) {
        bool visible;
        if (m_craftingNode->isVisible()) {
            plasma::Vector<2, int> cell = m_craftingWidget->getSelectedCell();
            visible = true;
        } else {
            visible = false;
        }
        m_blueprintNode->setVisible(visible);
    }

    // The equipment slots show with the inventory, each with the sprite of its item.
    m_equipmentOpen = m_inventoryNode->isVisible();
    m_characterWidget->m_node->m_parent->setVisible(m_equipmentOpen);
    for (std::vector<plasma::Node *>::iterator it = m_equipmentBoxes.begin(); it != m_equipmentBoxes.end(); it++) {
        (*it)->setVisible(m_equipmentOpen);
        (*it)->setText(L"count", L"", true);
    }
    Equipment &equipment = m_player->m_entityData.equipment;
    SpriteManager &sprites = m_world.m_spriteManager;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonWeaponLeft])->m_sprite =
        sprites.itemSprite(&equipment.weaponLeft);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonWeaponLeft])->m_item =
        &m_player->m_entityData.equipment.weaponLeft;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonWeaponRight])->m_sprite =
        sprites.itemSprite(&equipment.weaponRight);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonWeaponRight])->m_item =
        &m_player->m_entityData.equipment.weaponRight;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonRingLeft])->m_sprite =
        sprites.itemSprite(&equipment.ringLeft);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonRingLeft])->m_item =
        &m_player->m_entityData.equipment.ringLeft;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonRingRight])->m_sprite =
        sprites.itemSprite(&equipment.ringRight);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonRingRight])->m_item =
        &m_player->m_entityData.equipment.ringRight;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonNeck])->m_sprite = sprites.itemSprite(&equipment.neck);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonNeck])->m_item = &m_player->m_entityData.equipment.neck;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonShoulder])->m_sprite =
        sprites.itemSprite(&equipment.shoulder);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonShoulder])->m_item =
        &m_player->m_entityData.equipment.shoulder;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonChest])->m_sprite =
        sprites.itemSprite(&equipment.chest);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonChest])->m_item =
        &m_player->m_entityData.equipment.chest;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonHands])->m_sprite =
        sprites.itemSprite(&equipment.hands);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonHands])->m_item =
        &m_player->m_entityData.equipment.hands;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonFeet])->m_sprite = sprites.itemSprite(&equipment.feet);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonFeet])->m_item = &m_player->m_entityData.equipment.feet;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonLamp])->m_sprite = sprites.itemSprite(&equipment.lamp);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonLamp])->m_item = &m_player->m_entityData.equipment.lamp;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonSpecial])->m_sprite =
        sprites.itemSprite(&equipment.special);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonSpecial])->m_item =
        &m_player->m_entityData.equipment.special;
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonPet])->m_sprite = sprites.itemSprite(&equipment.pet);
    static_cast<SpriteWidget *>(m_equipmentSlots[EquipmentButtonPet])->m_item = &m_player->m_entityData.equipment.pet;

    // The pointer node follows the pointer.
    m_cursorNode->setVisible(isCursorVisible());
    m_cursorNode->transformation()->m_translation = m_engine->getPointer();
    m_cursorNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);

    // One received chat line per frame: the speaker's name in cyan, then the text.
    if (!m_chatMessages.empty()) {
        if (m_chatMessages.back().creatureId >= 0 && m_world.creatureByID(m_chatMessages.back().creatureId)) {
            Creature *speaker = m_world.creatureByID(m_chatMessages.back().creatureId);
            m_chatWidget->printMessage(widenString(std::string(speaker->m_entityData.name)) + L": ",
                                       plasma::Vector<3, unsigned char>(0, 255, 255));
        }
        m_chatWidget->printMessage(m_chatMessages.back().text + L"\n", plasma::Vector<3, unsigned char>(255, 255, 255));
        m_chatMessages.pop_back();
    }

    // Received items go into the inventory one every 100 ms, with sound 45.
    m_spiritTimer += milliseconds;
    if (!m_disenchantSpirits.empty() && m_spiritTimer > 100) {
        m_spiritTimer = 0;
        m_player->m_inventory.add(m_disenchantSpirits.front());
        playSound(SoundPickup, m_player->m_entityData.position, 1.0f, 1.0f);
        m_disenchantSpirits.pop_front();
        m_inventoryWidget->updateRows();
        m_inventoryWidget->updateScrollButton();
    }

    // The frame's updates. The local server's queued hits, buffs and projectiles join them, and the
    // world steps 20 ms at a time.
    ServerUpdates updates;
    m_frameMilliseconds = milliseconds;
    if (m_localServer) {
        m_localServer->takeHits(updates.hits);
        m_localServer->takeBuffs(updates.buffs);
        m_localServer->takeBullets(updates.projectiles);
    }
    if (m_seed == m_world.getSeed() && m_worldName == m_world.getName()) {
        if (m_player)
            m_inventoryWidget->m_inventory = &m_player->m_inventory;
        int step = milliseconds;
        while (step > 20) {
            m_world.update(20, &updates, 0);
            step -= 20;
        }
        m_world.update(step, &updates, 0);

        // The map discovers the four zones nearest the player (sampled 64 blocks to either side of it).
        for (int x = 0; x < 0x200; x += 0x100) {
            for (int y = 0; y < 0x200; y += 0x100) {
                m_worldMap.discover((int)(m_player->m_entityData.position[0] + x / 2 - 0x40) / BlocksPerZone,
                                    (int)(m_player->m_entityData.position[1] + y / 2 - 0x40) / BlocksPerZone);
            }
        }

        // The mission of the player's region record shows in the small quest tag while the player is
        // within its influence; a new or changed mission plays "show" and sound 46. A counting mission
        // fills the quest bar.
        static MissionState shownMission;
        Region::Record *record =
            m_world.getRecord((int)m_player->m_entityData.position[0] / BlocksPerZone / ZonesPerSceneryRecord,
                              (int)m_player->m_entityData.position[1] / BlocksPerZone / ZonesPerSceneryRecord);
        if (record) {
            float influence = record->influence(m_player->m_entityData.position[0], m_player->m_entityData.position[1]);
            // Mission type 1 (a monster) fades over 130 blocks around it.
            if (record->mission.kind == MissionMonster) {
                float distance = plasma::Vector<2, float>(
                                     (float)(Fixed(record->monster.zone[0] * BlocksPerZone + BlocksPerZone / 2) -
                                             m_player->m_entityData.position[0]),
                                     (float)(Fixed(record->monster.zone[1] * BlocksPerZone + BlocksPerZone / 2) -
                                             m_player->m_entityData.position[1]))
                                     .squaredLength() /
                                 16900.0f;
                if (distance > 1.0f)
                    distance = 1.0f;
                influence = (1.0f - distance) * (1.0f - distance);
            }
            if (influence > 0.0f) {
                if (record->mission.kind != MissionNone && record->mission.progress != MissionCompleted &&
                    influence > 0.1f) {
                    if (record->mission.progress == MissionInProgress)
                        m_worldMap.discover(record->mission.targetZone[0], record->mission.targetZone[1]);
                    if (shownMission.kind != record->mission.kind ||
                        shownMission.targetZone != record->mission.targetZone ||
                        shownMission.seed != record->mission.seed ||
                        shownMission.progress != record->mission.progress) {
                        shownMission = record->mission;
                        if (record->mission.progress == MissionInProgress) {
                            m_smallQuestTag->play(L"show", 0);
                            m_smallQuestTag->setText(L"objective", missionText(record), true);
                            playSound(SoundDisenchant2, m_cameraPosition, 1.0f, 1.0f);
                        }
                    }
                    m_smallQuestTag->setVisible(true);
                    if (record->mission.targetKillCount != 0 && record->mission.killCount != 0) {
                        m_questBar->setVisible(true);
                        m_questBarFill->transformation()->m_deformation.value().identity();
                        m_questBarFill->transformation()->m_deformation.value().scale(
                            (float)record->mission.killCount / (float)record->mission.targetKillCount, 1.0f, 1.0f);
                        m_questBarFill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
                    } else {
                        m_questBar->setVisible(false);
                    }
                } else if (record->mission.progress == MissionCompleted || influence < 0.05f) {
                    m_smallQuestTag->setVisible(false);
                    m_questBar->setVisible(false);
                }

                // The map tiles of the player's region take the region's cells.
                int regionX = (int)m_player->m_entityData.position[0] / BlocksPerZone / ZonesPerRegion;
                int regionY = (int)m_player->m_entityData.position[1] / BlocksPerZone / ZonesPerRegion;
                Region *region = m_world.getRegion(regionX, regionY);
                for (int x = 0; x < 64; x++) {
                    for (int y = 0; y < 64; y++) {
                        ZoneTile *tile = m_worldMap.getZoneTile(regionX * 64 + x, regionY * 64 + y);
                        if (tile)
                            tile->m_cell = region->m_cells[x * 64 + y];
                    }
                }
            } else {
                m_smallQuestTag->setVisible(false);
                shownMission = MissionState();
            }
        } else {
            m_smallQuestTag->setVisible(false);
            shownMission = MissionState();
        }

        // The landscape the player is in shows its name (at most every two seconds it is marked for
        // rebuilding).
        World::RegionRecord *nearest = m_world.nearestRegionRecord((int)m_player->m_entityData.position[0],
                                                                   (int)m_player->m_entityData.position[1]);
        m_worldMap.enterRegions();
        LandscapeTile *landscape = m_worldMap.nearestLandscape((int)m_player->m_entityData.position[0],
                                                               (int)m_player->m_entityData.position[1]);
        if (landscape && nearest && landscape->m_info.column == nearest->column) {
            landscape->m_discovered = true;
            static int lastLandscapeRebuild;
            int clock = m_world.getClock();
            if (clock - lastLandscapeRebuild > 2000) {
                lastLandscapeRebuild = clock;
                landscape->m_needsRebuild = 1;
            }
        }
        m_worldMap.leaveRegions();
    }

    // Zones received with their items: the zone takes the list (and is no longer awaited), and the
    // frame keeps the lists.
    if (!m_serverUpdates.items.empty()) {
        for (std::list<ServerUpdates::ItemListPacket>::iterator it = m_serverUpdates.items.begin();
             it != m_serverUpdates.items.end(); it++) {
            Zone *zone = m_world.zone(it->zone[0], it->zone[1]);
            if (zone) {
                zone->m_items.clear();
                zone->m_items.insert(zone->m_items.end(), it->items.begin(), it->items.end());
                zone->m_needsServerUpdate = false;
            }
        }
        updates.items.insert(updates.items.end(), m_serverUpdates.items.begin(), m_serverUpdates.items.end());
    }

    // The item under the pointer: the hovered ground item, or in the ring menu its selected entry.
    Item *hoveredItem = 0;
    if (m_hoveredGroundItem[0] >= 0) {
        Zone::ZoneItem *groundItem = m_world.item(m_hoveredGroundItem);
        if (groundItem)
            hoveredItem = &groundItem->item;
    }
    bool previewShown = false;
    if (m_ringMenuOpen) {
        std::vector<plasma::Vector<2, int>> usable = collectUsableItems();
        if (!usable.empty()) {
            unsigned int selected = m_ringMenuSelection;
            if ((int)selected < 0)
                selected =
                    (unsigned int)((selected + (-(int)selected / usable.size() + 1) * usable.size()) % usable.size());
            plasma::Vector<2, int> slot = usable[selected % usable.size()];
            hoveredItem = &m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]].item;
        }
    } else {
        // Otherwise the item under the pointer in the inventory, the equipment, the crafting, trade,
        // preview or identifier panels shows in the preview.
        plasma::Vector<2, int> cell = getHoveredSlot();
        Item *item;
        if (cell[0] >= 0 && cell[1] >= 0) {
            hoveredItem = &m_player->m_inventory.m_inventoryTabs[cell[0]][cell[1]].item;
            previewShown = true;
        } else if ((item = getHoveredEquipment()) != 0 && item->category != ItemCategoryNone) {
            hoveredItem = item;
            previewShown = true;
        } else if ((item = getHoveredFormula()) != 0) {
            hoveredItem = item;
            previewShown = true;
        } else if ((item = getHoveredVendorItem()) != 0) {
            hoveredItem = item;
            previewShown = true;
        } else if ((item = getHoveredIngredient()) != 0) {
            hoveredItem = item;
            previewShown = true;
        } else if ((item = getHoveredEnchantItem()) != 0) {
            hoveredItem = item;
            previewShown = true;
        }
    }
    m_previewNode->setVisible(hoveredItem != 0);
    // The title screen closes the game's panels.
    if (m_startScreen->isVisible()) {
        m_skillsOpen = false;
        m_inventoryNode->setVisible(false);
        m_adaptionPanel->setVisible(false);
        m_voxelPanel->setVisible(false);
    }

    // The skill screen: the two specializations and eleven skill buttons placed on the panel, with
    // their icons; the hovered one shows in the preview with a cyan frame. A skill whose prerequisite
    // has fewer than five points is greyed.
    m_skillPanel->setVisible(m_skillsOpen);
    m_previewWidget->m_skill = -1;
    m_previewWidget->m_specialization = -1;
    if (m_skillsOpen) {
        int left = (int)(m_skillPanel->widget()->viewX() + 50.0f);
        int top = (int)(m_skillPanel->widget()->viewY() + 70.0f);
        plasma::Transformation *transformation = m_specializationButtons[SpecializationFirst]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)left, (float)top) - transformation->m_pivot.value();
        m_specializationButtons[SpecializationFirst]->transformation()->attributesChanged(
            plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_specializationButtons[SpecializationSecond]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left + 70), (float)top) - transformation->m_pivot.value();
        m_specializationButtons[SpecializationSecond]->transformation()->attributesChanged(
            plasma::ATTRIBUTES_CHANGED_ALL);
        // The skills: the class's four in a row, then the rest in rows of two.
        int row = top + 80;
        transformation = m_skillButtons[SkillClassAbilityFirst]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left), (float)(row)) - transformation->m_pivot.value();
        m_skillButtons[SkillClassAbilityFirst]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_skillButtons[SkillClassAbilitySecond]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left + 55), (float)(row)) - transformation->m_pivot.value();
        m_skillButtons[SkillClassAbilitySecond]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_skillButtons[SkillClassAbilityThird]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left + 110), (float)(row)) - transformation->m_pivot.value();
        m_skillButtons[SkillClassAbilityThird]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_skillButtons[SkillClassAbilityFourth]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left + 165), (float)(row)) - transformation->m_pivot.value();
        m_skillButtons[SkillClassAbilityFourth]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_skillButtons[SkillPetTaming]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left), (float)(row + 60)) - transformation->m_pivot.value();
        m_skillButtons[SkillPetTaming]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_skillButtons[SkillPetRiding]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left + 55), (float)(row + 60)) - transformation->m_pivot.value();
        m_skillButtons[SkillPetRiding]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_skillButtons[SkillClimbing]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left), (float)(row + 120)) - transformation->m_pivot.value();
        m_skillButtons[SkillClimbing]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_skillButtons[SkillHangGliding]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left + 55), (float)(row + 120)) - transformation->m_pivot.value();
        m_skillButtons[SkillHangGliding]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_skillButtons[SkillSwimming]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left), (float)(row + 180)) - transformation->m_pivot.value();
        m_skillButtons[SkillSwimming]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_skillButtons[SkillBoatDriving]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(left + 55), (float)(row + 180)) - transformation->m_pivot.value();
        m_skillButtons[SkillBoatDriving]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);

        for (int i = 0; i < PlayerSpecializationCount; i++) {
            m_specializationButtons[i]->setVisible(true);
            if (m_specializationButtons[i]->widget()->isPointerOver()) {
                m_specializationButtons[i]->findNode(L"frame")->display()->setFillColor(
                    plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f));
                m_previewWidget->m_item = 0;
                m_previewWidget->m_specialization = i;
                m_previewNode->setVisible(true);
                previewShown = true;
            } else {
                m_specializationButtons[i]->findNode(L"frame")->display()->setFillColor(
                    plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
            }
            plasma::Texture *icon = m_specializationIcons[std::make_pair((int)m_player->m_entityData.classType, i)];
            if (icon) {
                int texture = icon->id();
                smoothMeshShape(m_specializationButtons[i]->getShape())->m_texture = texture;
                float saturation = i == m_skillWidget->m_specialization ? 1.0f : 0.0f;
                smoothMeshShape(m_specializationButtons[i]->getShape())->m_textureSaturation = saturation;
            } else {
                int texture = -1;
                smoothMeshShape(m_specializationButtons[i]->getShape())->m_texture = texture;
            }
            smoothMeshShape(m_specializationButtons[i]->getShape())->updateDrawingTextures();
        }

        for (int i = 0; i < SkillCount; i++) {
            m_skillButtons[i]->setVisible(i != SkillClassAbilityFifth);
            if (m_skillButtons[i]->widget()->isPointerOver()) {
                m_skillButtons[i]->findNode(L"frame")->display()->setFillColor(
                    plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f));
                m_previewWidget->m_item = 0;
                m_previewWidget->m_skill = i;
                m_previewWidget->m_skillLevel = m_skillWidget->m_skillLevels[i];
                m_previewNode->setVisible(true);
                previewShown = true;
            } else {
                m_skillButtons[i]->findNode(L"frame")->display()->setFillColor(
                    plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
            }
            int level = m_skillWidget->m_skillLevels[i];
            bool locked = false;
            if (i == SkillClassAbilitySecond) {
                if (m_skillWidget->m_skillLevels[SkillClassAbilityFirst] < 5)
                    locked = true;
            } else if (i == SkillClassAbilityThird) {
                if (m_skillWidget->m_skillLevels[SkillClassAbilitySecond] < 5)
                    locked = true;
            } else if (i == SkillClassAbilityFourth) {
                if (m_skillWidget->m_skillLevels[SkillClassAbilityThird] < 5)
                    locked = true;
            } else if (i == SkillPetRiding) {
                if (m_skillWidget->m_skillLevels[SkillPetTaming] < 5)
                    locked = true;
            } else if (i == SkillHangGliding) {
                if (m_skillWidget->m_skillLevels[SkillClimbing] < 5)
                    locked = true;
            } else if (i == SkillBoatDriving) {
                locked = m_skillWidget->m_skillLevels[SkillSwimming] < 5;
            }
            std::wstringstream text;
            text << level;
            m_skillButtons[i]->setText(text.str());
            // Skills 6 to 8 show their class's ability icons; the others have their own.
            plasma::Texture *icon;
            int classType = m_player->m_entityData.classType;
            switch (i) {
            case SkillClassAbilityFirst:
                switch (classType) {
                case ClassWarrior:
                    icon = m_abilityIcons[AbilitySmash];
                    break;
                case ClassRanger:
                    icon = m_abilityIcons[AbilityRangerKick];
                    break;
                case ClassMage:
                    icon =
                        m_abilityIcons[m_skillWidget->m_specialization == SpecializationWater ? AbilityHealingStream
                                                                                              : AbilityFireExplosion];
                    break;
                case ClassRogue:
                    icon = m_abilityIcons[AbilityIntercept];
                    break;
                default:
                    icon = 0;
                    break;
                }
                break;
            case SkillClassAbilitySecond:
                switch (classType) {
                case ClassWarrior:
                    icon = m_abilityIcons[AbilityCyclone];
                    break;
                case ClassRanger:
                    icon = m_abilityIcons[AbilityRetreat];
                    break;
                case ClassMage:
                    icon = m_abilityIcons[AbilityManaShield];
                    break;
                case ClassRogue:
                    icon = m_abilityIcons[AbilitySneak];
                    break;
                default:
                    icon = 0;
                    break;
                }
                break;
            case SkillClassAbilityThird:
                switch (classType) {
                case ClassWarrior:
                    icon = m_abilityIcons[m_skillWidget->m_specialization == SpecializationBerserker ? AbilityWarFrenzy
                                                                                                     : AbilityBulwark];
                    break;
                case ClassRanger:
                    icon = m_abilityIcons[m_skillWidget->m_specialization == SpecializationScout ? AbilitySwiftness
                                                                                                 : AbilityAim];
                    break;
                case ClassMage:
                    icon = m_abilityIcons[AbilityTeleport];
                    break;
                case ClassRogue:
                    icon = m_abilityIcons[m_skillWidget->m_specialization == SpecializationNinja ? AbilityShuriken
                                                                                                 : AbilityCamouflage];
                    break;
                default:
                    icon = 0;
                    break;
                }
                break;
            default:
                icon = m_skillIcons[i];
                break;
            }
            if (icon) {
                int texture = icon->id();
                smoothMeshShape(m_skillButtons[i]->getShape())->m_texture = texture;
                float saturation = locked ? 0.0f : 1.0f;
                smoothMeshShape(m_skillButtons[i]->getShape())->m_textureSaturation = saturation;
            } else {
                int texture = -1;
                smoothMeshShape(m_skillButtons[i]->getShape())->m_texture = texture;
            }
            smoothMeshShape(m_skillButtons[i]->getShape())->updateDrawingTextures();
        }
    } else {
        for (int i = 0; i < PlayerSpecializationCount; i++)
            m_specializationButtons[i]->setVisible(false);
    }
    m_skillButtons[SkillClassAbilityFourth]->setVisible(false);

    // The game menu's buttons brighten under the pointer.
    for (std::vector<plasma::Node *>::iterator it = m_menuButtons.begin(); it != m_menuButtons.end(); it++)
        (*it)->setVisible(m_gameMenuOpen);
    if (m_gameMenuOpen) {
        for (std::vector<plasma::Node *>::iterator it = m_menuButtons.begin(); it != m_menuButtons.end(); it++) {
            float brightness = (*it)->widget()->isPointerOver() ? 1.5f : 1.0f;
            smoothMeshShape((*it)->getShape())->m_textureBrightness = brightness;
            smoothMeshShape((*it)->getShape())->updateDrawingTextures();
        }
    }

    // The previews follow the pointer while it shows something, and otherwise wait at the bottom
    // right.
    if (previewShown) {
        plasma::Transformation *transformation = m_previewNode->transformation();
        transformation->m_translation =
            m_engine->getPointer() + plasma::Vector<2, float>(20.0f, 20.0f) - transformation->m_pivot.value();
        m_previewNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_equippedPreviewNode->transformation();
        transformation->m_translation =
            m_engine->getPointer() + plasma::Vector<2, float>(-235.0f, 20.0f) - transformation->m_pivot.value();
    } else {
        plasma::Transformation *transformation = m_previewNode->transformation();
        transformation->m_translation = plasma::Vector<2, float>((float)(m_engine->viewportWidth() - 320),
                                                                 (float)(m_engine->viewportHeight() - 320)) -
                                        transformation->m_pivot.value();
        m_previewNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_equippedPreviewNode->transformation();
        transformation->m_translation = plasma::Vector<2, float>((float)(m_engine->viewportWidth() - 575),
                                                                 (float)(m_engine->viewportHeight() - 320)) -
                                        transformation->m_pivot.value();
    }
    m_equippedPreviewNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);

    // The preview shows the hovered item and, to compare, the equipped items of its kind ("Currently
    // equipped").
    m_previewWidget->m_item = hoveredItem;
    m_previewWidget->m_equippedItems.clear();
    if (hoveredItem) {
        Equipment &worn = m_player->m_entityData.equipment;
        if (hoveredItem != &worn.weaponRight && hoveredItem != &worn.weaponLeft &&
            worn.weaponRight.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.weaponRight);
        if (hoveredItem != &m_player->m_entityData.equipment.weaponRight &&
            hoveredItem != &m_player->m_entityData.equipment.weaponLeft &&
            m_player->m_entityData.equipment.weaponLeft.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.weaponLeft);
        if (hoveredItem != &m_player->m_entityData.equipment.pet &&
            m_player->m_entityData.equipment.pet.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.pet);
        if (hoveredItem != &m_player->m_entityData.equipment.shoulder &&
            m_player->m_entityData.equipment.shoulder.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.shoulder);
        if (hoveredItem != &m_player->m_entityData.equipment.chest &&
            m_player->m_entityData.equipment.chest.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.chest);
        if (hoveredItem != &m_player->m_entityData.equipment.hands &&
            m_player->m_entityData.equipment.hands.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.hands);
        if (hoveredItem != &m_player->m_entityData.equipment.feet &&
            m_player->m_entityData.equipment.feet.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.feet);
        if (hoveredItem != &m_player->m_entityData.equipment.neck &&
            m_player->m_entityData.equipment.neck.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.neck);
        if (hoveredItem != &m_player->m_entityData.equipment.ringRight &&
            hoveredItem != &m_player->m_entityData.equipment.ringLeft &&
            m_player->m_entityData.equipment.ringRight.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.ringRight);
        if (hoveredItem != &m_player->m_entityData.equipment.ringRight &&
            hoveredItem != &m_player->m_entityData.equipment.ringLeft &&
            m_player->m_entityData.equipment.ringLeft.category == hoveredItem->category)
            m_previewWidget->m_equippedItems.push_back(&m_player->m_entityData.equipment.ringLeft);
    }
    m_equippedPreviewNode->setVisible(!m_previewWidget->m_equippedItems.empty() && m_previewNode->isVisible());

    // Crafting: holding the left button on the blueprint's craft button starts the formula; after a
    // second the item is made.
    if (m_blueprintNode && m_blueprintNode->isVisible()) {
        if (m_blueprintPreview->isCraftButtonHovered() && m_inputs[InputMouseLeft] && m_craftingProgress < 0.0f) {
            m_craftingProgress = 0.0f;
            m_craftingFormula = m_blueprintPreview->m_formula;
        }
        if (m_craftingProgress >= 0.0f) {
            m_craftingProgress += milliseconds * 0.001f;
            if (m_craftingProgress >= 1.0f) {
                m_craftingProgress = -1.0f;
                craft();
            }
        }
        m_blueprintPreview->setProgress(m_craftingProgress);
    }

    // Received block changes: set or removed (a block of type 0 removes), then the plants they no
    // longer carry go; the frame keeps them.
    if (!m_serverUpdates.blocks.empty()) {
        m_world.enterZones();
        int minimumX = m_serverUpdates.blocks.front().position[0];
        int minimumY = m_serverUpdates.blocks.front().position[1];
        int maximumX = minimumX;
        int maximumY = minimumY;
        for (std::list<ServerUpdates::BlockPacket>::iterator it = m_serverUpdates.blocks.begin();
             it != m_serverUpdates.blocks.end(); it++) {
            int &y = it->position[1];
            int &x = it->position[0];
            if (it->block.type == BlockAir)
                m_world.destroyBlock(x, y, it->position[2]);
            else
                m_world.setBlock(x, y, it->position[2], it->block, 0);
            if (x < minimumX)
                minimumX = x;
            if (x > maximumX)
                maximumX = x;
            if (y < minimumY)
                minimumY = y;
            if (y > maximumY)
                maximumY = y;
        }
        m_world.removeUnsupportedPlants(minimumX, minimumY, maximumX, maximumY);
        m_world.leaveZones();
        updates.blocks.insert(updates.blocks.end(), m_serverUpdates.blocks.begin(), m_serverUpdates.blocks.end());
    }

    // Received static objects (doors, chests...) replace the zone's.
    if (!m_serverUpdates.objects.empty()) {
        for (std::list<ServerUpdates::ZoneObjectPacket>::iterator it = m_serverUpdates.objects.begin();
             it != m_serverUpdates.objects.end(); it++) {
            Zone *zone = m_world.zone(it->key[0], it->key[1]);
            if (zone) {
                int index = it->key[2];
                if (index >= 0 && index < (int)zone->m_objects.size()) {
                    static_cast<Zone::ObjectState &>(zone->m_objects[index]) = it->object;
                    zone->m_needsServerUpdate = false;
                }
            }
        }
        updates.objects.insert(updates.objects.end(), m_serverUpdates.objects.begin(), m_serverUpdates.objects.end());
    }

    // Received buildings replace the zone's.
    if (!m_serverUpdates.buildings.empty()) {
        m_world.enterZones();
        for (std::list<ServerUpdates::BuildingListPacket>::iterator it = m_serverUpdates.buildings.begin();
             it != m_serverUpdates.buildings.end(); it++) {
            Zone *zone = m_world.zone(it->zone[0], it->zone[1]);
            if (zone) {
                zone->m_buildings.clear();
                for (std::list<Zone::Building>::iterator entry = it->buildings.begin(); entry != it->buildings.end();
                     entry++)
                    zone->m_buildings.push_back(*entry);
                zone->m_needsServerUpdate = false;
            }
        }
        m_world.leaveZones();
        updates.buildings.insert(updates.buildings.end(), m_serverUpdates.buildings.begin(),
                                 m_serverUpdates.buildings.end());
    }

    // Items the server says the player picked up (mana cubes are counted below).
    if (!m_serverUpdates.pickedUpItems.empty()) {
        for (std::list<ServerUpdates::ItemPickupPacket>::iterator it = m_serverUpdates.pickedUpItems.begin();
             it != m_serverUpdates.pickedUpItems.end(); it++) {
            Creature *creature = m_world.creatureByID(it->creatureId);
            if (creature && creature == m_player && it->item.category != ItemCategoryManaCube) {
                creature->m_inventory.add(it->item);
                m_inventoryWidget->updateRows();
                m_inventoryWidget->updateScrollButton();
            }
        }
        updates.pickedUpItems.insert(updates.pickedUpItems.end(), m_serverUpdates.pickedUpItems.begin(),
                                     m_serverUpdates.pickedUpItems.end());
    }

    // Every pickup of the frame: the player's refreshes the crafting screen; a category 13 item of the
    // player's gives ten xp per level; a mana cube raises the count (every fourth gives a skill
    // point); other items are announced.
    for (std::list<ServerUpdates::ItemPickupPacket>::iterator it = updates.pickedUpItems.begin();
         it != updates.pickedUpItems.end(); it++) {
        Creature *creature = m_world.creatureByID(it->creatureId);
        if (creature) {
            if (creature == m_player)
                updateFormulas();
            if (it->item.category == ItemCategoryPlatinumCoin && it->creatureId == m_player->m_id) {
                m_player->m_entityData.xp += it->item.level * 10;
                m_player->applyLevelUps(&m_world);
            }
            if (it->item.category == ItemCategoryManaCube) {
                m_player->m_entityData.manaCubeCount++;
                m_player->m_entityData.currentAbility = AbilityPresentItem;
                m_player->m_entityData.timeSinceAbility = 0;
                m_chatWidget->printMessage(L"You receive a mana cube.\n",
                                           plasma::Vector<3, unsigned char>(100, 150, 255));
                playSound(SoundMissionComplete, m_cameraPosition, 1.0f, 1.0f);
                if (m_player->m_entityData.manaCubeCount % 4 == 0) {
                    m_manaCubeBar->play(L"manacubes", 0);
                    m_chatWidget->printMessage(L"You gain one skill point.\n",
                                               plasma::Vector<3, unsigned char>(150, 128, 255));
                }
            } else {
                notifyItem(it->item, creature);
            }
        }
    }

    // Kills.
    if (!m_serverUpdates.kills.empty()) {
        for (std::list<KillPacket>::iterator it = m_serverUpdates.kills.begin(); it != m_serverUpdates.kills.end();
             it++)
            m_world.onKill(*it);
        updates.kills.insert(updates.kills.end(), m_serverUpdates.kills.begin(), m_serverUpdates.kills.end());
    }

    // Aggro between two other creatures.
    if (!m_serverUpdates.aggro.empty()) {
        for (std::list<AggroPacket>::iterator it = m_serverUpdates.aggro.begin(); it != m_serverUpdates.aggro.end();
             it++) {
            if (m_world.creatureByID(it->creatureId) && m_world.creatureByID(it->targetId) &&
                it->creatureId != it->targetId) {
                float value = it->aggro;
                m_world.creatureByID(it->creatureId)->m_aggro[it->targetId] = value;
            }
        }
    }

    // Buffs.
    if (!m_serverUpdates.buffs.empty()) {
        for (std::list<BuffPacket>::iterator it = m_serverUpdates.buffs.begin(); it != m_serverUpdates.buffs.end();
             it++) {
            if (m_world.creatureByID(it->targetId))
                m_world.creatureByID(it->targetId)->setBuff(it->buff);
        }
        updates.buffs.insert(updates.buffs.end(), m_serverUpdates.buffs.begin(), m_serverUpdates.buffs.end());
    }

    // Mission states: the record takes the state and its region is marked changed.
    if (!m_serverUpdates.missions.empty()) {
        for (std::list<ServerUpdates::MissionPacket>::iterator it = m_serverUpdates.missions.begin();
             it != m_serverUpdates.missions.end(); it++) {
            int &y = it->position[1];
            int &x = it->position[0];
            Region::Record *record = m_world.getRecord(x, y);
            if (record)
                record->mission = it->mission;
            Region *region = m_world.getRegion(x / 8, y / 8);
            if (region)
                region->m_needsServerUpdate = false;
        }
    }

    // Hits received for the player: a block (type 1) pushes the character and costs block power,
    // feeding mana and charge; a mana hit (type 5) drains the manashield buffs and reports them; any
    // other, while alive, takes stealth, stun time and hit points.
    for (std::list<HitPacket>::iterator it = m_serverUpdates.hits.begin(); it != m_serverUpdates.hits.end(); it++) {
        if (m_world.creatureByID(it->targetId) != m_player)
            continue;
        if (it->hitType != HitTypeMiss && it->hitType == HitTypeBlock && it->damage > 0.0f) {
            m_player->m_entityData.velocity = it->direction;
            m_player->m_hitFlash = 1.0f;
        }
        if (it->hitType == HitTypeBlock) {
            m_player->m_entityData.velocity = it->direction;
            float damage = it->damage;
            float absorbed = damage / m_player->blockPower();
            m_player->m_entityData.mp += m_player->m_entityData.blockPower;
            m_player->m_entityData.charge += m_player->m_entityData.blockPower;
            if (m_player->m_entityData.mp > 1.0f)
                m_player->m_entityData.mp = 1.0f;
            if (m_player->m_entityData.charge > m_player->m_entityData.mp)
                m_player->m_entityData.charge = m_player->m_entityData.mp;
            m_player->m_entityData.blockPower -= absorbed;
            if (m_player->m_entityData.blockPower < -1.0f)
                m_player->m_entityData.blockPower = -1.0f;
        } else if (it->hitType == HitTypeMana) {
            for (std::list<Creature::Buff>::iterator buff = m_player->m_buffs.begin(); buff != m_player->m_buffs.end();
                 buff++) {
                if (buff->type == BuffManashield) {
                    buff->strength -= it->damage;
                    if (buff->strength <= 0.0f) {
                        buff->strength = 0.0f;
                        buff->timeRemaining = 0;
                    }
                    BuffPacket packet;
                    packet.sourceId = m_player->m_id;
                    packet.targetId = m_player->m_id;
                    packet.buff = *buff;
                    updates.buffs.push_back(packet);
                }
            }
        } else if (m_player->m_entityData.hp > 0.0f) {
            if (it->damage > 0.0f) {
                float loss = it->damage * 0.25f;
                m_player->m_stealth -= loss / pow(2.0f, (m_player->m_entityData.level - 1) * 0.25f);
                if (m_player->m_stealth < 0.0f)
                    m_player->m_stealth = 0.0f;
            }
            if (it->stunTime > 0)
                m_player->m_entityData.stunTime = std::max(it->stunTime, m_player->m_entityData.stunTime);
            m_player->m_entityData.hp -= it->damage;
            if (m_player->m_entityData.hp < 0.0f)
                m_player->m_entityData.hp = 0.0f;
            if (m_player->m_entityData.hp > m_player->maxHP())
                m_player->m_entityData.hp = m_player->maxHP();
        }
    }

    // Other creatures' projectiles and hits join the world's and the frame's; the received sounds,
    // particles and missions join the frame's. The received lists are then emptied.
    for (std::list<BulletPacket>::iterator it = m_serverUpdates.projectiles.begin();
         it != m_serverUpdates.projectiles.end(); it++) {
        if (it->ownerId != m_player->m_id)
            m_world.m_projectiles.push_back(*it);
    }
    for (std::list<HitPacket>::iterator it = m_serverUpdates.hits.begin(); it != m_serverUpdates.hits.end(); it++) {
        if (it->attackerId != m_player->m_id)
            updates.hits.push_back(*it);
    }
    updates.particles.insert(updates.particles.end(), m_serverUpdates.particles.begin(),
                             m_serverUpdates.particles.end());
    updates.sounds.insert(updates.sounds.end(), m_serverUpdates.sounds.begin(), m_serverUpdates.sounds.end());
    updates.missions.insert(updates.missions.end(), m_serverUpdates.missions.begin(), m_serverUpdates.missions.end());
    m_serverUpdates.hits.clear();
    m_serverUpdates.sounds.clear();
    m_serverUpdates.particles.clear();
    m_serverUpdates.blocks.clear();
    m_serverUpdates.projectiles.clear();
    m_serverUpdates.items.clear();
    m_serverUpdates.objects.clear();
    m_serverUpdates.buildings.clear();
    m_serverUpdates.pickedUpItems.clear();
    m_serverUpdates.kills.clear();
    m_serverUpdates.aggro.clear();
    m_serverUpdates.buffs.clear();
    m_serverUpdates.missions.clear();

    // The frame's mission events: a started mission (state 1) with its creature, and a completed one
    // (state 2) shines the quest tag with sound 30.
    for (std::list<ServerUpdates::MissionPacket>::iterator it = updates.missions.begin(); it != updates.missions.end();
         it++) {
        if (it->mission.kind == MissionNone)
            continue;
        if (it->mission.progress == MissionInProgress && m_world.creatureByID(it->creatureId))
            talkTo(m_world.creatureByID(it->creatureId), true);
        if (it->mission.progress == MissionCompleted) {
            Region::Record *record = m_world.getRecord(it->position[0], it->position[1]);
            if (record) {
                m_questTag->play(L"shine", 0);
                m_questTag->setText(L"objective", missionText(record), true);
                playSound(SoundMissionComplete, m_cameraPosition, 1.0f, 1.0f);
            }
        }
    }

    // The dialogs' reading clocks run; the objective list is rebuilt below.
    for (std::vector<SpeechWidget *>::iterator it = m_speechWidgets.begin(); it != m_speechWidgets.end(); it++)
        (*it)->m_readTime += milliseconds;
    m_objectiveWidget->unused160.clear();
    m_dialogWidget->m_readTime += milliseconds;

    // The living creatures within 60 blocks of the player, nearest first (their life bars and names
    // are drawn below).
    std::vector<NearbyCreature> nearby;
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         it++) {
        Creature *&creature = it->second;
        if (creature && creature->m_entityData.hp > 0.0f) {
            float distanceSquared =
                plasma::Vector<3, float>(creature->m_drawnPosition - m_player->m_entityData.position).squaredLength();
            if (distanceSquared <= 3600.0f) {
                NearbyCreature entry;
                entry.creature = creature;
                entry.distanceSquared = distanceSquared;
                entry.labelShown = false;
                nearby.push_back(entry);
            }
        }
    }
    std::sort(nearby.begin(), nearby.end(), NearbyCreature::Nearer());
    // The camera's forward direction: the cursor ray of the picking.
    plasma::Vector<3, float> cursorRay = m_cameraMatrix.transformDirection(plasma::Vector<3, float>(0.0f, 0.0f, 1.0f));
    cursorRay.normalize();
    m_questTag->setVisible(m_questTag->isPlaying());

    // The experience bar: "xp/next" and the fill.
    if (m_experienceBar) {
        std::wstringstream text;
        text << m_player->m_entityData.xp << L"/" << m_player->maxExperience();
        m_experienceBar->setText(L"text", text.str(), true);
        text.str(L"");
        plasma::Transformation *transformation = m_experienceBar->transformation();
        transformation->m_translation = plasma::Vector<2, float>(100.0f, 75.0f) - transformation->m_pivot.value();
        m_experienceBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_experienceBarFill->transformation()->m_deformation.value().identity();
        float fill = (float)m_player->m_entityData.xp / (float)m_player->maxExperience() > 1.0f
                         ? 1.0f
                         : (float)m_player->m_entityData.xp / (float)m_player->maxExperience();
        m_experienceBarFill->transformation()->m_deformation.value().scale(fill, 1.0f, 1.0f);
        m_experienceBarFill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }

    // The mana cubes: one lit per cube towards the next skill point (all four while the bar plays its
    // Animation).
    if (m_manaCubeBar) {
        plasma::Transformation *transformation = m_manaCubeBar->transformation();
        transformation->m_translation = plasma::Vector<2, float>(100.0f, 130.0f) - transformation->m_pivot.value();
        m_manaCubeBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        int lit = m_player->m_entityData.manaCubeCount % 4;
        if (m_manaCubeBar->isPlaying())
            lit = 4;
        for (std::list<plasma::Node *>::iterator it = m_manaCubeBar->m_children.begin();
             it != m_manaCubeBar->m_children.end(); it++) {
            (*it)->setVisible(lit > 0);
            lit--;
        }
    }

    // The player's life bar: "hp/max", the name, the level and class, and the fill.
    if (m_lifeBar) {
        std::wstringstream text;
        text << (int)m_player->m_entityData.hp << L"/" << (int)m_player->maxHP();
        m_lifeBar->setText(L"text", text.str(), true);
        plasma::Transformation *transformation = m_lifeBar->transformation();
        transformation->m_translation = plasma::Vector<2, float>(100.0f, 30.0f) - transformation->m_pivot.value();
        m_lifeBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_lifeBar->setText(L"info", widenString(std::string(m_player->m_entityData.name)), true);
        text.str(L"");
        text << L"LVL " << m_player->m_entityData.level;
        switch (m_player->m_entityData.classType) {
        case ClassWarrior:
            text << " Warrior";
            break;
        case ClassRanger:
            text << " Ranger";
            break;
        case ClassMage:
            text << " Mage";
            break;
        case ClassRogue:
            text << " Rogue";
            break;
        }
        m_lifeBar->setText(L"class", text.str(), true);
        m_lifeBarFill->transformation()->m_deformation.value().identity();
        m_lifeBarFill->transformation()->m_deformation.value().scale(m_player->m_entityData.hp / m_player->maxHP(),
                                                                     1.0f, 1.0f);
        m_lifeBarFill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }

    // The companion's bars: life (name, level and race), its experience, and the riding stamina in
    // five segments.
    Creature *companion = m_world.creatureByID(m_player->m_companionId);
    if (companion) {
        m_companionLifeBar->setVisible(true);
        m_companionExperienceBar->setVisible(true);
        m_ridingBar->setVisible(true);
        std::wstringstream text;
        text << (int)companion->m_entityData.hp << L"/" << (int)companion->maxHP();
        m_companionLifeBar->setText(L"text", text.str(), true);
        plasma::Transformation *transformation = m_companionLifeBar->transformation();
        transformation->m_translation = plasma::Vector<2, float>(100.0f, 180.0f) - transformation->m_pivot.value();
        m_companionLifeBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_companionLifeBar->setText(L"info", widenString(std::string(companion->m_entityData.name)), true);
        text.str(L"");
        text << L"LVL " << companion->m_entityData.level << L" " << m_world.creatureKey(companion->m_entityData.race);
        m_companionLifeBar->setText(L"class", text.str(), true);
        m_companionLifeBarFill->transformation()->m_deformation.value().identity();
        m_companionLifeBarFill->transformation()->m_deformation.value().scale(
            companion->m_entityData.hp / companion->maxHP(), 1.0f, 1.0f);
        m_companionLifeBarFill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        text.str(L"");
        text << companion->m_entityData.xp << L"/" << companion->maxExperience();
        m_companionExperienceBar->setText(L"text", text.str(), true);
        transformation = m_companionExperienceBar->transformation();
        transformation->m_translation = plasma::Vector<2, float>(100.0f, 225.0f) - transformation->m_pivot.value();
        m_companionExperienceBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_companionExperienceBarFill->transformation()->m_deformation.value().identity();
        float fill = (float)companion->m_entityData.xp / (float)companion->maxExperience() > 1.0f
                         ? 1.0f
                         : (float)companion->m_entityData.xp / (float)companion->maxExperience();
        m_companionExperienceBarFill->transformation()->m_deformation.value().scale(fill, 1.0f, 1.0f);
        m_companionExperienceBarFill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        transformation = m_ridingBar->transformation();
        transformation->m_translation = plasma::Vector<2, float>(100.0f, 290.0f) - transformation->m_pivot.value();
        m_ridingBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        int segments = std::min((int)(m_player->m_ridingStamina * 5.0f + 0.9999f), 5);
        for (std::list<plasma::Node *>::iterator it = m_ridingBar->m_children.begin();
             it != m_ridingBar->m_children.end(); it++) {
            (*it)->setVisible(segments > 0);
            segments--;
        }
    } else {
        m_companionLifeBar->setVisible(false);
        m_companionExperienceBar->setVisible(false);
        m_ridingBar->setVisible(false);
    }

    // The party's life bars: the other players, from 310 pixels down.
    for (std::vector<plasma::Node *>::iterator it = m_partyLifeBars.begin(); it != m_partyLifeBars.end(); it++)
        (*it)->setVisible(false);
    int partyIndex = 0;
    int partyBarX = 310;
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         it++) {
        Creature *&member = it->second;
        if (!member)
            continue;
        if (partyIndex >= (int)m_partyLifeBars.size())
            break;
        if (member->m_entityData.hostilityType != HostilityPlayer || member == m_player)
            continue;
        std::wstringstream text;
        text << (int)member->m_entityData.hp << L"/" << (int)member->maxHP();
        m_partyLifeBars[partyIndex]->setVisible(true);
        m_partyLifeBars[partyIndex]->setText(L"text", text.str(), true);
        text.str(L"");
        text << widenString(std::string(member->m_entityData.name));
        m_partyLifeBars[partyIndex]->setText(L"info", text.str(), true);
        text.str(L"");
        text << L"LVL " << member->m_entityData.level;
        switch (member->m_entityData.classType) {
        case ClassWarrior:
            text << " Warrior";
            break;
        case ClassRanger:
            text << " Ranger";
            break;
        case ClassMage:
            text << " Mage";
            break;
        case ClassRogue:
            text << " Rogue";
            break;
        }
        m_partyLifeBars[partyIndex]->setText(L"class", text.str(), true);
        plasma::Transformation *transformation = m_partyLifeBars[partyIndex]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)partyBarX, 30.0f) - transformation->m_pivot.value();
        m_partyLifeBars[partyIndex]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        partyBarX += 220;
        m_partyLifeBarFills[partyIndex]->transformation()->m_deformation.value().identity();
        m_partyLifeBarFills[partyIndex]->transformation()->m_deformation.value().scale(
            member->m_entityData.hp / member->maxHP(), 1.0f, 1.0f);
        m_partyLifeBarFills[partyIndex]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        partyIndex++;
    }

    // The quick item: the selected usable item's count on the last ability button and its sprite; Q
    // uses it (sound 50 when its level is above the player's). Without usable items the ring menu
    // closes.
    std::vector<plasma::Vector<2, int>> usable = collectUsableItems();
    if (usable.empty())
        m_ringMenuOpen = false;
    bool cursorShown = isCursorVisible();
    if (!usable.empty() && !cursorShown) {
        unsigned int selected = m_ringMenuSelection;
        if ((int)selected < 0)
            selected =
                (unsigned int)((selected + (-(int)selected / usable.size() + 1) * usable.size()) % usable.size());
        plasma::Vector<2, int> slot = usable[selected % usable.size()];
        std::wstringstream text;
        text << m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]].quantity;
        m_abilityButtons.back()->setText(L"count", text.str(), true);
        static bool useWasDown;
        if (m_inputs[InputKeyQ] && !useWasDown) {
            ItemStack &stack = m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]];
            if (stack.item.level > m_player->m_entityData.level)
                playSound(SoundHumanVoice02, m_cameraPosition, 1.0f, 1.0f);
            else
                useItem(&m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]]);
        }
        useWasDown = m_inputs[InputKeyQ];
        slot = usable[selected % usable.size()];
        m_quickItemWidget->m_sprite = sprites.itemSprite(&m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]].item);
        m_quickItemWidget->m_item = &m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]].item;
    } else {
        m_abilityButtons.back()->setText(L"count", L" ", true);
        m_quickItemWidget->m_sprite = 0;
        m_quickItemWidget->m_item = 0;
    }

    // The ability buttons' frames light red while their input is held (and the pointer hidden).
    plasma::Vector<4, float> held(1.0f, 0.0f, 0.2f, 1.0f);
    plasma::Vector<4, float> idle(1.0f, 1.0f, 1.0f, 1.0f);
    m_abilityButtons[AbilityBarMouseLeft]->findNode(L"frame")->display()->setFillColor(
        !cursorShown && m_inputs[InputMouseLeft] ? plasma::Vector<4, float>(1.0f, 0.0f, 0.2f, 1.0f)
                                                 : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_abilityButtons[AbilityBarMouseRight]->findNode(L"frame")->display()->setFillColor(
        !cursorShown && m_inputs[InputMouseRight] ? plasma::Vector<4, float>(1.0f, 0.0f, 0.2f, 1.0f)
                                                  : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_abilityButtons[AbilityBarKey1]->findNode(L"frame")->display()->setFillColor(
        !cursorShown && m_inputs[InputKey1] ? plasma::Vector<4, float>(1.0f, 0.0f, 0.2f, 1.0f)
                                            : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_abilityButtons[AbilityBarKey2]->findNode(L"frame")->display()->setFillColor(
        !cursorShown && m_inputs[InputKey2] ? plasma::Vector<4, float>(1.0f, 0.0f, 0.2f, 1.0f)
                                            : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_abilityButtons[AbilityBarKey3]->findNode(L"frame")->display()->setFillColor(
        !cursorShown && m_inputs[InputKey3] ? plasma::Vector<4, float>(1.0f, 0.0f, 0.2f, 1.0f)
                                            : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_abilityButtons[AbilityBarKey4]->findNode(L"frame")->display()->setFillColor(
        !cursorShown && m_inputs[InputKey4] ? plasma::Vector<4, float>(1.0f, 0.0f, 0.2f, 1.0f)
                                            : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_abilityButtons.back()->findNode(L"frame")->display()->setFillColor(
        !cursorShown && m_inputs[InputKeyQ] ? plasma::Vector<4, float>(1.0f, 0.0f, 0.2f, 1.0f)
                                            : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));

    // The ring menu's selector 200 pixels above the centre.
    if (m_selector) {
        plasma::Transformation *transformation = m_selector->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight / 2 - 200)) -
            transformation->m_pivot.value();
        m_selector->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_selector->setVisible(m_ringMenuOpen);
    }

    // The crosshair at the centre while the pointer is hidden; the zoom crosshair while aiming (toggle
    // 0x400).
    if (m_crosshair) {
        m_crosshair->setVisible(!isCursorVisible() && !(m_player->m_entityData.binaryToggles.sneaking) &&
                                !m_inputs[InputKeyShift]);
        plasma::Transformation *transformation = m_crosshair->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight / 2)) -
            transformation->m_pivot.value();
        m_crosshair->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    if (m_zoomCrosshair) {
        m_zoomCrosshair->setVisible(!isCursorVisible() && (m_player->m_entityData.binaryToggles.sneaking));
        plasma::Transformation *transformation = m_zoomCrosshair->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight / 2)) -
            transformation->m_pivot.value();
        m_zoomCrosshair->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }

    // The mana bar ("mp/100") with the charge, right of the centre at the bottom.
    if (m_manaBar) {
        std::wstringstream text;
        text << (int)(m_player->m_entityData.mp * 100.0f + 0.5f) << L"/100";
        m_manaBar->setText(L"text", text.str(), true);
        plasma::Transformation *transformation = m_manaBar->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2 + 8), (float)(m_windowHeight - 90)) -
            transformation->m_pivot.value();
        m_manaBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_manaBarFill->transformation()->m_deformation.value().identity();
        m_manaBarFill->transformation()->m_deformation.value().scale(m_player->m_entityData.mp, 1.0f, 1.0f);
        m_manaBarFill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_chargeBar->transformation()->m_deformation.value().identity();
        m_chargeBar->transformation()->m_deformation.value().scale(m_player->m_entityData.charge, 1.0f, 1.0f);
        m_chargeBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }

    // The hit point bar, left of the centre.
    if (m_hpBar) {
        std::wstringstream text;
        text << (int)m_player->m_entityData.hp << L"/" << (int)m_player->maxHP();
        m_hpBar->setText(L"text", text.str(), true);
        plasma::Transformation *transformation = m_hpBar->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2 - 163), (float)(m_windowHeight - 90)) -
            transformation->m_pivot.value();
        m_hpBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        m_hpBarFill->transformation()->m_deformation.value().identity();
        m_hpBarFill->transformation()->m_deformation.value().scale(m_player->m_entityData.hp / m_player->maxHP(), 1.0f,
                                                                   1.0f);
        m_hpBarFill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }

    // The stamina bar while stamina is below full.
    if (m_staminaBar) {
        if (m_player->m_stamina < 1.0f) {
            m_staminaBar->setVisible(true);
            plasma::Transformation *transformation = m_staminaBar->transformation();
            transformation->m_translation =
                plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight - 115)) -
                transformation->m_pivot.value();
            m_staminaBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
            m_staminaBarFill->transformation()->m_deformation.value().identity();
            m_staminaBarFill->transformation()->m_deformation.value().scale(m_player->m_stamina, 1.0f, 1.0f);
            m_staminaBarFill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        } else {
            m_staminaBar->setVisible(false);
        }
    }

    // The cast bar: stealth (for non-casters), the block power while blocking or recovering, or the
    // anticipation of a named spell. It hides when there is nothing to show.
    if (m_castBar) {
        Creature::EntityData &caster = m_player->m_entityData;
        bool hidden = false;
        if (!(caster.blockPower < 1.0f)) {
            unsigned char ability = caster.currentAbility;
            bool spell =
                ability == AbilityTeleportToCity || ability == AbilityFireSalvo || ability == AbilityWaterSalvo ||
                ability == AbilityWandFirebolt || ability == AbilityFirebolt || ability == AbilityBraceletFirebolt ||
                ability == AbilityFireball || ability == AbilityFireRay || ability == AbilityFireBeam ||
                ability == AbilityExplosion || ability == AbilityLava || ability == AbilitySummon ||
                ability == AbilityWandWaterDrop || ability == AbilityWaterDrop || ability == AbilityBraceletWaterDrop ||
                ability == AbilityWaterSplash || ability == AbilityFireSwirl || ability == AbilityFireVortex ||
                ability == AbilityWaterSwirl || ability == AbilityWaterVortex || ability == AbilityHealingStream ||
                ability == AbilityFireExplosion || ability == AbilityTeleport;
            if ((!spell || m_player->m_entityData.timeSinceAbility > m_player->anticipationTime()) &&
                !(m_player->m_stealth > 0.0f) &&
                (!m_player->isBlocking() || m_player->m_entityData.blockPower == 0.0f)) {
                m_castBar->setVisible(false);
                hidden = true;
            }
        }
        if (!hidden) {
            m_castBar->setVisible(true);
            plasma::Transformation *transformation = m_castBar->transformation();
            transformation->m_translation =
                plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight - 150)) -
                transformation->m_pivot.value();
            m_castBar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
            m_castBarFill->transformation()->m_deformation.value().identity();
            // Stealth first; only without it the cast or the block power.
            if (!m_player->isCaster() && m_player->m_stealth > 0.0f) {
                m_castBarFill->transformation()->m_deformation.value().scale(m_player->m_stealth, 1.0f, 1.0f);
                m_castBar->setText(L"name", L"Stealth", true);
            } else if (!m_player->isBlocking() && !(1.0f > m_player->m_entityData.blockPower)) {
                m_castBarFill->transformation()->m_deformation.value().scale(
                    (float)m_player->m_entityData.timeSinceAbility / (float)m_player->anticipationTime(), 1.0f, 1.0f);
                switch (m_player->m_entityData.currentAbility) {
                case AbilityFireSwirl:
                    m_castBar->setText(L"name", L"Fire Swirl", true);
                    break;
                case AbilityFireVortex:
                    m_castBar->setText(L"name", L"Fire Vortex", true);
                    break;
                case AbilityWaterSwirl:
                    m_castBar->setText(L"name", L"Water Swirl", true);
                    break;
                case AbilityWaterVortex:
                    m_castBar->setText(L"name", L"Water Vortex", true);
                    break;
                case AbilityHealingStream:
                    m_castBar->setText(L"name", L"Healing Stream", true);
                    break;
                case AbilityFireball:
                    m_castBar->setText(L"name", L"Fireball", true);
                    break;
                case AbilityWandFirebolt:
                case AbilityFirebolt:
                case AbilityBraceletFirebolt:
                    m_castBar->setText(L"name", L"Firebolt", true);
                    break;
                case AbilityWaterDrop:
                case AbilityBraceletWaterDrop:
                case AbilityWandWaterDrop:
                    m_castBar->setText(L"name", L"Water drop", true);
                    break;
                case AbilityWaterSplash:
                    m_castBar->setText(L"name", L"Water splash", true);
                    break;
                case AbilityWaterSalvo:
                    m_castBar->setText(L"name", L"Water Salvo", true);
                    break;
                case AbilityFireSalvo:
                    m_castBar->setText(L"name", L"Fire Salvo", true);
                    break;
                case AbilityTeleport:
                    m_castBar->setText(L"name", L"Teleport", true);
                    break;
                case AbilityExplosion:
                    m_castBar->setText(L"name", L"Explosion", true);
                    break;
                case AbilityFireExplosion:
                    m_castBar->setText(L"name", L"Fire Explosion", true);
                    break;
                case AbilityLava:
                    m_castBar->setText(L"name", L"Lava", true);
                    break;
                case AbilitySummon:
                    m_castBar->setText(L"name", L"Summon", true);
                    break;
                case AbilityFireBeam:
                    m_castBar->setText(L"name", L"Firebeam", true);
                    break;
                case AbilityFireRay:
                    m_castBar->setText(L"name", L"Fireray", true);
                    break;
                case AbilityTeleportToCity:
                    m_castBar->setText(L"name", L"Teleport to City", true);
                    break;
                default:
                    m_castBar->setText(L"name", L"", true);
                    break;
                }
            } else {
                float blockPower = m_player->m_entityData.blockPower;
                if (0.0f > blockPower)
                    blockPower = 0.0f;
                m_castBarFill->transformation()->m_deformation.value().scale(blockPower, 1.0f, 1.0f);
                m_castBar->setText(L"name", L"Block Power", true);
            }
            m_castBarFill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        }
    }

    // The combo counter: "N HITS", cyan with "!" at the maximum combo, played once per change.
    static unsigned char shownCombo;
    if (m_player->m_entityData.hitCombo != 0 && m_player->m_entityData.hitCombo != shownCombo) {
        std::wstringstream text;
        text << L"     " << m_player->m_entityData.hitCombo
             << (m_player->m_entityData.hitCombo == 1 ? L" HIT" : L" HITS");
        bool maximum = false;
        if (m_player->m_entityData.hitCombo >= m_player->maxCombo()) {
            text << "!";
            maximum = true;
        }
        std::list<plasma::TextShape *> shapes;
        m_comboPoints->collectTextShapes(shapes);
        for (std::list<plasma::TextShape *>::iterator it = shapes.begin(); it != shapes.end(); it++)
            (*it)->m_color = plasma::Vector<4, float>(maximum ? 0.0f : 1.0f, 1.0f, 1.0f, 1.0f);
        m_comboPoints->setText(text.str());
        m_comboPoints->play(L"hit", 0);
    }
    shownCombo = (unsigned char)m_player->m_entityData.hitCombo;

    // A level up rises "LEVEL UP!" over the player with sound 29 and teaches the level's formulas;
    // gained xp rises "XP +n" and is reported in the chat.
    if (m_player->m_entityData.level != m_player->m_shownLevel) {
        TextFX levelUp;
        levelUp.color = plasma::Vector<4, float>(0.8f, 0.6f, 1.0f, 1.0f);
        levelUp.textFlags = 1 << plasma::TEXT_RIGHT;
        levelUp.position = m_player->m_entityData.position;
        levelUp.age = 0;
        levelUp.duration = 3000;
        levelUp.offsetX = 0.0f;
        levelUp.offsetY = -250.0f;
        levelUp.text = L"LEVEL UP!";
        levelUp.pixelSize = 50.0f;
        levelUp.rise = -100.0f;
        m_textFx.push_back(levelUp);
        playSound(SoundLevelUp2, m_player->m_entityData.position, 1.0f, 1.0f);
        m_player->m_shownLevel = m_player->m_entityData.level;
        m_player->m_shownXp = 0;
        learnFormulas(m_player->m_entityData.level);
    }
    if ((int)m_player->m_shownXp < m_player->m_entityData.xp) {
        TextFX gain;
        gain.color = plasma::Vector<4, float>(0.8f, 0.6f, 1.0f, 1.0f);
        gain.textFlags = 1 << plasma::TEXT_RIGHT;
        gain.position = m_player->m_entityData.position;
        gain.age = 0;
        gain.duration = 3000;
        gain.offsetX = 0.0f;
        gain.offsetY = -200.0f;
        std::wstringstream text;
        text << "XP +" << m_player->m_entityData.xp - (int)m_player->m_shownXp;
        gain.text = text.str();
        gain.pixelSize = 30.0f;
        gain.rise = -100.0f;
        m_textFx.push_back(gain);
        text.str(L"");
        text << L"You gain " << m_player->m_entityData.xp - (int)m_player->m_shownXp << L" XP." << std::endl;
        m_chatWidget->printMessage(text.str(), plasma::Vector<3, unsigned char>(150, 128, 255));
        m_player->m_shownXp = m_player->m_entityData.xp;
    }
    m_comboPoints->setVisible(m_comboPoints->isPlaying());

    // The place names: the landscape and region at the player, top right, and the name banner that
    // fades in and out over eight seconds when the place changes.
    plasma::Transformation *landscapeTransformation = m_landscapeNode->transformation();
    landscapeTransformation->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth - 260), 90.0f) - landscapeTransformation->m_pivot.value();
    m_landscapeNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    static std::wstring shownCellName;
    std::wstring cellName = m_world.m_speech.cellName(&m_world, (int)m_player->m_entityData.position[0],
                                                      (int)m_player->m_entityData.position[1]);
    std::wstring landscapeName = m_world.m_speech.landscapeName(&m_world, (int)m_player->m_entityData.position[0],
                                                                (int)m_player->m_entityData.position[1]);
    static std::wstring shownLandscapeName;
    std::wstringstream infoText;
    m_landscapeNode->setText(L"landscape", landscapeName, true);
    m_landscapeNode->setText(L"landscapedetail", cellName, true);
    static int nameBannerTime;
    if (cellName == shownCellName && landscapeName == shownLandscapeName && nameBannerTime == 0) {
        m_landNameNode->setVisible(false);
    } else {
        int time = nameBannerTime;
        if (time == 0) {
            shownCellName = cellName;
            shownLandscapeName = landscapeName;
            m_landNameNode->setText(L"name", landscapeName, true);
            m_landNameNode->setText(L"detail", cellName, true);
            time = 8000;
        }
        float phase = ((float)time / 8000.0f - 0.5f) * 2.0f;
        m_landNameNode->display()->setFillColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f - phase * phase));
        m_landNameNode->setVisible(true);
        nameBannerTime = time - milliseconds;
        if (nameBannerTime < 0)
            nameBannerTime = 0;
    }

    // The information text: time of day, temperature and humidity at the player. The region's stars
    // are hidden.
    plasma::Transformation *infoTransformation = m_infoNode->transformation();
    infoTransformation->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth - 15), 20.0f) - infoTransformation->m_pivot.value();
    m_infoNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    {
        std::wstringstream &text = infoText;
        text.str(L"");
        int humidity = (int)(m_world.blendedHumidity((int)m_player->m_entityData.position[0],
                                                     (int)m_player->m_entityData.position[1]) *
                             100.0f);
        int temperature = -20 - (int)(m_world.blendedTemperature((int)m_player->m_entityData.position[0],
                                                                 (int)m_player->m_entityData.position[1]) *
                                      -60.0f);
        text << L"TIME " << m_world.getTime() / 3600000 << L":" << std::setfill(L'0') << std::setw(2)
             << m_world.getTime() / 60000 % 60 << L"  TEMP " << temperature << L" \u00b0C  HUM " << humidity << L"%";
        m_landscapeNode->findNode(L"star1")->setVisible(false);
        m_landscapeNode->findNode(L"star2")->setVisible(false);
        m_landscapeNode->findNode(L"star3")->setVisible(false);
        m_landscapeNode->findNode(L"star4")->setVisible(false);
        m_infoNode->setText(L"info", text.str(), true);
    }

    // Each creature's damage taken within the last ten seconds (for its life bar).
    for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin(); it != m_world.m_creatures.end();
         it++) {
        Creature *&creature = it->second;
        if (creature) {
            creature->m_timeSinceDamageDealt += milliseconds;
            if (creature->m_timeSinceDamageDealt > 10000) {
                creature->m_damageDealtTime = 0;
                creature->m_damageDealt = 0;
            }
            creature->m_damageDealtTime += milliseconds;
        }
    }

    // The frame's hits: damage counts for the attacker's life bar; the player locks onto the first
    // enemy it hits; feeding a pet food to a tamed creature (hit type 6, a buff 7 of the player at
    // full strength) makes it the player's companion; powered hits flash in the weapons' effect
    // colours and critical ones in pale blue.
    for (std::list<HitPacket>::iterator it = updates.hits.begin(); it != updates.hits.end(); it++) {
        if (!m_world.creatureByID(it->targetId))
            continue;
        if (m_world.creatureByID(it->attackerId) && it->damage > 0.0f) {
            float damage = it->damage;
            m_world.creatureByID(it->attackerId)->m_damageDealt += damage;
            m_world.creatureByID(it->attackerId)->m_timeSinceDamageDealt = 0;
        }
        if (it->attackerId == m_player->m_id &&
            m_world.creatureByID(it->targetId)->m_entityData.hostilityType == HostilityEnemy && !m_target)
            m_target = m_world.creatureByID(it->targetId);
        if (it->targetId == m_player->m_id && it->hitType == HitTypeTameness &&
            m_player->m_entityData.equipment.pet.category == ItemCategoryPetFood) {
            Creature *tamed = m_world.creatureByID(it->attackerId);
            for (std::list<Creature::Buff>::iterator buff = tamed->m_buffs.begin(); buff != tamed->m_buffs.end();
                 buff++) {
                if (buff->type == BuffTameness && buff->strength >= 1.0f && buff->sourceId == m_player->m_id) {
                    m_player->m_entityData.equipment.pet = Item();
                    m_player->m_entityData.currentAbility = AbilityNone;
                    m_player->m_companionId = tamed->m_id;
                    m_player->m_entityData.equipment.pet.category = ItemCategoryPet;
                    m_player->m_entityData.equipment.pet.id = (unsigned char)tamed->m_entityData.race;
                    m_player->m_entityData.equipment.pet.level = (short)tamed->m_entityData.level;
                    m_player->m_entityData.equipment.pet.modifier = tamed->m_entityData.xp;
                    break;
                }
            }
        }
        if (it->powered && m_world.creatureByID(it->attackerId)) {
            std::list<Item *> weapons = m_world.creatureByID(it->attackerId)->attackWeapons();
            for (std::list<Item *>::iterator weapon = weapons.begin(); weapon != weapons.end(); weapon++) {
                plasma::Vector<4, float> color = (*weapon)->effectColor();
                if (color[3] > 0.0f) {
                    Light light;
                    light.position = it->position;
                    light.radius = WeaponHitLightRadius;
                    light.color = plasma::Vector<3, float>(color[0], color[1], color[2]);
                    light.color = light.color;
                    light.age = 0;
                    light.duration = HitLightDuration;
                    m_lights.push_back(light);
                }
            }
        }
        if (it->supportRayLight && m_world.creatureByID(it->attackerId)) {
            Light light;
            light.position = it->position;
            light.radius = SupportRayLightRadius;
            light.color = plasma::Vector<3, float>(0.3f, 0.3f, 0.5f);
            light.age = 0;
            light.duration = HitLightDuration;
            m_lights.push_back(light);
        }
        if (m_world.creatureByID(it->attackerId) != m_player && m_world.creatureByID(it->targetId) != m_player)
            continue;
        // The player's hits and the hits on the player float their damage: green for healing, red on the
        // player, yellow when powered; a critical hit rises further in larger type.
        unsigned char hitType = it->hitType;
        if (hitType == HitTypeDodge || hitType == HitTypeTameness)
            continue;
        if (!(fabs(it->damage) >= 0.1f) && hitType == HitTypeDamage)
            continue;
        TextFX damageText;
        damageText.position = it->position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 2.0f));
        damageText.position[2] +=
            Fixed(1.0f - m_world.creatureByID(it->targetId)->m_entityData.appearance.size[2] * 0.5f);
        if (0.0f > it->damage) {
            damageText.color = plasma::Vector<4, float>(0.4f, 1.0f, 0.5f, 1.0f);
            if (m_world.creatureByID(it->targetId) == m_player) {
                damageText.rise = 100.0f;
                damageText.position[2] -= Fixed(1);
            } else {
                damageText.rise = -100.0f;
            }
        } else if (m_world.creatureByID(it->targetId) == m_player) {
            damageText.color = plasma::Vector<4, float>(1.0f, 0.3f, 0.3f, 1.0f);
            damageText.rise = 100.0f;
            damageText.position[2] -= Fixed(1);
        } else {
            damageText.color = plasma::Vector<4, float>(1.0f, 1.0f, it->powered ? 0.25f : 1.0f, 1.0f);
            damageText.rise = -100.0f;
            if (it->critical)
                damageText.rise = -200.0f;
        }
        damageText.textFlags = 1 << plasma::TEXT_CENTER;
        damageText.age = 0;
        damageText.offsetX = 0.0f;
        damageText.offsetY = 0.0f;
        damageText.pixelSize = 20.0f;
        if (it->critical)
            damageText.pixelSize = 60.0f;
        std::wstringstream text;
        if (it->hitType == HitTypeBlock) {
            text << "BLOCK" << std::endl;
        } else if (it->hitType == HitTypeMiss) {
            text << "MISS" << std::endl;
        } else if (it->hitType == HitTypeMana) {
            text << "ABSORB" << std::endl;
        } else {
            float amount = fabs(it->damage);
            if (1.0f > amount)
                text << (float)(int)(amount * 10.0f) * 0.1f;
            else
                text << (int)amount;
        }
        damageText.text = text.str();
        m_textFx.push_back(damageText);
    }

    // The frame's particle bursts: count particles each, spread randomly around the burst's velocity
    // and spinning; type 1 burns in random oranges.
    for (std::list<ServerUpdates::ParticlePacket>::iterator it = updates.particles.begin();
         it != updates.particles.end(); it++) {
        for (int i = 0; i < it->count; i++) {
            Particle particle;
            particle.type = it->type;
            particle.age = 0;
            particle.position = it->position;
            // The original draws z, y, x in that order.
            particle.velocity = it->acceleration + it->spread * 2.0f *
                                                       plasma::Vector<3, float>(rand() / float(RAND_MAX) - 0.5f,
                                                                                rand() / float(RAND_MAX) - 0.5f,
                                                                                rand() / float(RAND_MAX) - 0.5f);
            particle.rotation = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
            // The original draws z, y, x in that order.
            particle.spin =
                100.0f * plasma::Vector<3, float>(rand() / float(RAND_MAX) - 0.5f, rand() / float(RAND_MAX) - 0.5f,
                                                  rand() / float(RAND_MAX) - 0.5f);
            particle.color = it->color;
            if (it->type == ParticleFire)
                particle.color = plasma::Vector<4, float>(1.0f, rand() / float(RAND_MAX), 0.0f, 1.0f);
            particle.size = it->size;
            particle.matrix.identity();
            m_particles.push_front(particle);
        }
    }

    // The frame's sounds; sounds 81 and 82 shake the camera.
    for (std::list<ServerUpdates::SoundPacket>::iterator it = updates.sounds.begin(); it != updates.sounds.end();
         it++) {
        playSound(it->sound, plasma::Vector<3, Fixed>(it->position), it->volume, it->pitch);
        if (it->sound == SoundExplosion || it->sound == SoundPunch282)
            m_screenShake = 1.0f;
    }

    // Changed blocks remesh their chunk and its neighbours. A block outside the world ends the frame
    // here (the code returns).
    int remeshTime = timeGetTime();
    for (std::list<ServerUpdates::BlockPacket>::iterator it = updates.blocks.begin(); it != updates.blocks.end();
         it++) {
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int chunkX = it->position[0] / 32 + dx;
                int chunkY = it->position[1] / 32 + dy;
                if (chunkX < 0 || chunkY < 0 || chunkX * BlocksPerChunk >= BlocksPerWorld ||
                    chunkY * BlocksPerChunk >= BlocksPerWorld)
                    return;
                Chunk *chunk = &m_chunks[(chunkY % m_viewDistance) * m_viewDistance + chunkX % m_viewDistance];
                if (chunk->position == plasma::Vector<2, int>(chunkX, chunkY)) {
                    EnterCriticalSection(&chunk->lock);
                    chunk->needsRemesh = true;
                    chunk->remeshTime = remeshTime;
                    LeaveCriticalSection(&chunk->lock);
                }
            }
        }
    }

    // The floating texts are rebuilt as text nodes each frame: a dark outline copy and the text,
    // rising and fading over their duration; expired ones are removed.
    m_textFxNode->destroyChildren();
    std::list<std::list<TextFX>::iterator> expiredTexts;
    for (std::list<TextFX>::iterator it = m_textFx.begin(); it != m_textFx.end(); it++) {
        TextFX &fx = *it;
        plasma::Vector<3, float> view =
            m_viewMatrix *
            plasma::Vector<3, float>(fx.position + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0)));
        if (view[2] > 0.0f) {
            m_projectionMatrix.transform(view);
            view *= (0.5f * plasma::Vector<3, float>((float)m_engine->viewportWidth(),
                                                     (float)-m_engine->viewportHeight(), 1.0f));
            view += 0.5f *
                    plasma::Vector<3, float>((float)m_engine->viewportWidth(), (float)m_engine->viewportHeight(), 0.0f);
            float progress = (float)fx.age / (float)fx.duration;
            plasma::TextShape *outline = m_engine->createTextShape(fx.text, L"");
            outline->m_color = fx.color * plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f);
            outline->m_pixelSize = fx.pixelSize;
            outline->m_strokeColor = fx.color * plasma::Vector<4, float>(0.1f, 0.1f, 0.1f, 1.0f - progress);
            outline->m_strokeRadius = fx.pixelSize * 0.125f;
            outline->m_fontName = L"resource1.dat";
            outline->setFlags(fx.textFlags);
            outline->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
            plasma::Node *node = m_engine->createNode(0, outline, 0, m_textFxNode, L"");
            node->transformation()->m_translation = plasma::Vector<2, float>(view[0], view[1]) +
                                                    plasma::Vector<2, float>(fx.offsetX, fx.offsetY) +
                                                    plasma::Vector<2, float>(0.0f, fx.rise * progress);
            node->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
            plasma::TextShape *shape = m_engine->createTextShape(fx.text, L"");
            shape->m_color = fx.color * plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f - progress);
            shape->m_pixelSize = fx.pixelSize;
            shape->m_fontName = L"resource1.dat";
            shape->setFlags(fx.textFlags);
            shape->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
            node = m_engine->createNode(0, shape, 0, m_textFxNode, L"");
            node->transformation()->m_translation = plasma::Vector<2, float>(view[0], view[1]) +
                                                    plasma::Vector<2, float>(fx.offsetX, fx.offsetY) +
                                                    plasma::Vector<2, float>(0.0f, fx.rise * progress + 0.0f);
            node->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        }
        fx.age += milliseconds;
        if (fx.age > fx.duration)
            expiredTexts.push_back(it);
    }
    for (std::list<std::list<TextFX>::iterator>::iterator it = expiredTexts.begin(); it != expiredTexts.end(); it++)
        m_textFx.erase(*it);

    // The particles (at most 5000): they age, fall (type 4 does not), collide with blocks and spin.
    std::list<std::list<Particle>::iterator> expiredParticles;
    if ((int)m_particles.size() > 5000)
        m_particles.resize(5000);
    for (std::list<Particle>::iterator it = m_particles.begin(); it != m_particles.end(); it++) {
        Particle &particle = *it;
        particle.age += milliseconds;
        if (particle.age >= 0 && !particle.motionDisabled && particle.type != ParticleStationary) {
            if (particle.type != ParticleLeaf)
                particle.velocity[2] -= milliseconds * 0.001f * ParticleGravity;
            float seconds = milliseconds * 0.001f;
            particle.rotation += seconds * particle.spin;
            if (particle.collidesWithBlocks) {
                // Moved in up to ten steps, axis by axis, stopping at solid blocks (leaves pass leaf and tree
                // blocks); landing stops it.
                plasma::Vector<3, float> step = seconds * particle.velocity;
                int steps = std::min((int)step.length() + 1, 10);
                step *= 1.0f / steps; // the frame's motion in parts
                for (int n = steps; n > 0; n--) {
                    for (int axis = 0; axis < 3; axis++) {
                        particle.position[axis] += Fixed(step[axis]);
                        Block block =
                            m_world.getBlock(particle.position[0], particle.position[1], particle.position[2], 0);
                        if (block.isSolid() && (particle.type != ParticleLeaf ||
                                                (block.blockType() != BlockLeaves && block.blockType() != BlockTree))) {
                            // The motion along the axis stops; landing also stops the spin.
                            particle.position[axis] -= Fixed(step[axis]);
                            particle.velocity[axis] = 0.0f;
                            if (axis == 2) {
                                particle.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                                particle.spin = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                            }
                        }
                    }
                }
            } else {
                particle.position += plasma::Vector<3, Fixed>(seconds * particle.velocity);
                particle.rotation += seconds * particle.spin;
            }
        }
        if (particle.age > particle.lifetime)
            expiredParticles.push_back(it);
    }
    for (std::list<std::list<Particle>::iterator>::iterator it = expiredParticles.begin(); it != expiredParticles.end();
         it++)
        m_particles.erase(*it);

    // The lights age and expire.
    std::list<std::list<Light>::iterator> expiredLights;
    for (std::list<Light>::iterator it = m_lights.begin(); it != m_lights.end(); it++) {
        it->age += milliseconds;
        if (it->age > it->duration)
            expiredLights.push_back(it);
    }
    for (std::list<std::list<Light>::iterator>::iterator it = expiredLights.begin(); it != expiredLights.end(); it++)
        m_lights.erase(*it);

    // The upgrade and identifier panels keep the inventory open; a closed inventory closes its side
    // panels.
    if (m_enchantPanel->isVisible() || m_adaptionPanel->isVisible())
        m_inventoryNode->setVisible(true);
    if (!m_inventoryNode->isVisible()) {
        m_tradePanel->setVisible(false);
        m_enchantPanel->setVisible(false);
        m_adaptionPanel->setVisible(false);
    }
    // Trading ends five blocks away.
    if (m_tradeCreature &&
        plasma::Vector<3, float>(m_tradeCreature->m_entityData.position - m_player->m_entityData.position)
                .squaredLength() >= 25.0f)
        m_tradeCreature = 0;

    // The camera's turn, distance and zoom follow their targets; the turn's rate comes from the camera
    // smoothness option.
    blend(m_cameraRotation, m_targetCameraRotation, milliseconds, pow(0.005f, m_options.cameraSmoothness * 0.01f));
    blend(m_targetCameraDistance, m_cameraDistance, milliseconds, 0.004f);
    blend(m_mapZoom, m_targetMapZoom, milliseconds, 0.004f);

    // A click outside the interface with a carried stack drops one item of it.
    static bool clickWasDown;
    if (m_inputs[InputMouseLeft] && !clickWasDown && m_player->m_inventory.m_cursor.quantity != 0 &&
        m_inventoryNode->isVisible() && !m_engine->getPointerNode()) {
        Interaction drop;
        drop.type = InteractionDrop;
        drop.item = m_player->m_inventory.m_cursor.item;
        m_player->m_interactions.push_back(drop);
        m_player->m_inventory.m_cursor.removeOne();
    }
    clickWasDown = m_inputs[InputMouseLeft];
    static int interactionDelay;
    interactionDelay = std::max(interactionDelay - milliseconds, 0);

    // A speech bubble without choices moves on to its next text three seconds after the text was read;
    // once its texts are done the bubble closes and forgets its speaker.
    for (int i = 0; i < (int)m_speechWidgets.size(); i++) {
        if (m_speechWidgets[i]->m_choices.empty() &&
            m_speechWidgets[i]->m_readTime > m_speechWidgets[i]->getDialogDuration() + 3000) {
            m_speechWidgets[i]->m_dialogIndex++;
            m_speechWidgets[i]->m_readTime = 0;
            m_speechWidgets[i]->m_textUncovered = 0;
        }
        if (m_speechWidgets[i]->m_dialogIndex >= (int)m_speechWidgets[i]->m_dialogs.size()) {
            m_speechBubbles[i]->setVisible(false);
            m_speechWidgets[i]->m_creature = 0;
        }
    }
    if (m_dialogWidget->m_choices.empty() && m_dialogWidget->m_readTime > m_dialogWidget->getDialogDuration() + 3000) {
        m_dialogWidget->m_dialogIndex++;
        m_dialogWidget->m_readTime = 0;
        m_dialogWidget->m_textUncovered = 0;
    }
    if (m_dialogWidget->m_dialogIndex >= (int)m_dialogWidget->m_dialogs.size()) {
        m_dialogNode->setVisible(false);
        m_dialogWidget->m_creature = 0;
    }

    // The prompt under the crosshair. A dead player revives. A friendly creature selected for
    // interaction talks (R starts the conversation, or closes a finished one); the player's own pet is
    // ridden (ability 106) with the riding skill. Otherwise the hovered object names its use, unless
    // the player occupies it.
    static bool interactWasDown;
    m_crosshair->setText(L"");
    if (m_player->m_entityData.hp <= 0.0f)
        m_crosshair->setText(L"[R] Revive");
    Creature *partner = m_world.creatureByID(m_interactionCreatureId);
    if (partner && partner->m_entityData.hostilityType == HostilityFriendlyNpc && !cursorShown) {
        m_crosshair->setText(L"[R] Talk");
        if (m_inputs[InputKeyR] && !interactWasDown) {
            // The code asks here too, and ignores the answer.
            if (m_conversationWidget)
                m_conversationWidget->isDialogFinished();
            Interaction talk;
            talk.position = partner->m_entityData.spawnSlot;
            talk.type = InteractionTalk;
            m_player->m_interactions.push_back(talk);
            if (m_conversationWidget && m_conversationWidget->isDialogFinished()) {
                m_conversationWidget->m_creature = 0;
                m_conversationWidget->getNode()->setVisible(false);
                m_conversationWidget = 0;
            } else {
                talkTo(partner, false);
            }
        }
    } else if (partner && partner->m_entityData.hostilityType == HostilityPet && isPet(partner->m_entityData.race) &&
               m_player->m_entityData.skillLevels[SkillPetRiding] != 0 &&
               partner->m_entityData.ownerId == m_player->m_id &&
               m_player->m_entityData.currentAbility != AbilityRidePet) {
        m_crosshair->setText(L"[R] Ride");
        if (m_inputs[InputKeyR] && !interactWasDown) {
            m_player->m_entityData.currentAbility = AbilityRidePet;
            m_player->m_entityData.timeSinceAbility = 0;
        }
    } else {
        Zone::Object *object = m_world.object(m_hoveredObject);
        if (object) {
            switch (object->type) {
            case StaticObjectDoor:
            case StaticObjectBigDoor:
            case StaticObjectWindow:
            case StaticObjectChest:
                if (object->active)
                    m_crosshair->setText(L"[R] Open");
                else
                    m_crosshair->setText(L"[R] Close");
                break;
            case StaticObjectStool:
            case StaticObjectBench:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Sit Down");
                break;
            case StaticObjectBed:
            case StaticObjectBeachTowel:
            case StaticObjectSleepingMat:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Sleep");
                break;
            case StaticObjectRuneStone:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Teleport");
                break;
            case StaticObjectFurnace:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Use Furnace");
                break;
            case StaticObjectAnvil:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Use Anvil");
                break;
            case StaticObjectSpinningWheel:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Use Spinning Wheel");
                break;
            case StaticObjectLoom:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Use Loom");
                break;
            case StaticObjectSaw:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Use Saw");
                break;
            case StaticObjectWorkbench:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Use Workbench");
                break;
            case StaticObjectCustomizationBench:
                if (object->occupantId != m_player->m_id)
                    m_crosshair->setText(L"[R] Use Customization Bench");
                break;
            default:
                m_crosshair->setText(L"[R] Examine");
                break;
            }
        }
    }

    // Each speech bubble follows its speaker: its anchor glides towards the speaker's drawn position
    // (less the pending step height), and the bubble is placed on the screen above the speaker's head.
    for (int i = 0; i < (int)m_speechBubbles.size(); i++) {
        if (!m_speechWidgets[i]->m_creature)
            continue;
        float factor = blendFactor(milliseconds, 0.02f);
        plasma::Vector<3, Fixed> step(
            plasma::Vector<3, float>(0.0f, 0.0f, -m_speechWidgets[i]->m_creature->m_stepOffset));
        m_speechWidgets[i]->m_anchorPosition +=
            (m_speechWidgets[i]->m_creature->m_drawnPosition + step - m_speechWidgets[i]->m_anchorPosition) *
            Fixed(factor);
        plasma::Vector<3, float> view =
            m_viewMatrix *
            plasma::Vector<3, float>(
                m_speechWidgets[i]->m_anchorPosition +
                plasma::Vector<3, Fixed>(
                    m_drawOriginX, m_drawOriginY,
                    Fixed(m_speechWidgets[i]->m_creature->m_entityData.appearance.size[2] * 0.5f + 1.0f)));
        if (view[2] > 0.0f) {
            m_speechBubbles[i]->setVisible(true);
            m_projectionMatrix.transform(view);
            view *= 0.5f * plasma::Vector<3, float>((float)m_engine->viewportWidth(),
                                                    (float)-m_engine->viewportHeight(), 1.0f);
            view += 0.5f *
                    plasma::Vector<3, float>((float)m_engine->viewportWidth(), (float)m_engine->viewportHeight(), 0.0f);
            // The bubble's tail points down at the anchor: 125 x 170 pixels from the bubble's corner.
            m_speechBubbles[i]->transformation()->m_translation =
                plasma::Vector<2, float>(view[0], view[1]) - m_speechBubbles[i]->transformation()->m_pivot.value() -
                plasma::Vector<2, float>(125.0f, 170.0f);
            m_speechBubbles[i]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        } else {
            m_speechBubbles[i]->setVisible(false);
        }
    }

    // With the map closed, its teleport selection is cleared.
    if (!m_mapOpen) {
        m_worldMap.m_canTeleport = false;
        m_worldMap.m_selectedZone = plasma::Vector<2, int>(-1, -1);
    }
    // R (outside the ring menu) revives a dead player at the respawn point with full hit points, or
    // opens the selected creature's interaction and uses the hovered object: a rune stone opens the
    // map to teleport, the customization bench its panel with the inventory, a crafting station
    // toggles the crafting panel on its tab; anything else is used through the world (or closes a
    // finished conversation).
    if (!m_ringMenuOpen && m_inputs[InputKeyR] && !interactWasDown) {
        if (m_player->m_entityData.hp <= 0.0f) {
            m_player->stopActions();
            m_player->m_entityData.position = m_world.respawnPosition(m_player->m_entityData.position);
            m_player->m_entityData.hp = m_player->maxHP();
        } else {
            openInteraction();
            if (m_hoveredObject[0] >= 0) {
                Zone::Object *object = m_world.object(m_hoveredObject);
                if (object) {
                    if (object->type == StaticObjectRuneStone) {
                        m_mapOpen = true;
                        m_worldMap.m_canTeleport = true;
                    } else if (object->type == StaticObjectCustomizationBench) {
                        m_voxelWidget->cancelUpgrade();
                        m_voxelPanel->setVisible(!m_voxelPanel->isVisible());
                        m_inventoryNode->setVisible(m_voxelPanel->isVisible());
                    } else if (object->type == StaticObjectFurnace || object->type == StaticObjectAnvil ||
                               object->type == StaticObjectSaw || object->type == StaticObjectWorkbench ||
                               object->type == StaticObjectSpinningWheel || object->type == StaticObjectLoom ||
                               object->type == StaticObjectCampfire) {
                        m_craftingNode->setVisible(!m_craftingNode->isVisible());
                        switch (object->type) {
                        case StaticObjectCampfire:
                            m_craftingWidget->setTabIndex(CraftingTabCooking);
                            break;
                        case StaticObjectFurnace:
                        case StaticObjectSpinningWheel:
                        case StaticObjectSaw:
                            m_craftingWidget->setTabIndex(CraftingTabMaterials);
                            break;
                        }
                        updateFormulas();
                    } else {
                        // The code asks here too, and ignores the answer.
                        if (m_conversationWidget)
                            m_conversationWidget->isDialogFinished();
                        Interaction use;
                        use.position = m_hoveredObject;
                        use.type = InteractionUseObject;
                        m_player->m_interactions.push_back(use);
                        if (m_conversationWidget && m_conversationWidget->isDialogFinished()) {
                            m_conversationWidget->m_creature = 0;
                            m_conversationWidget->getNode()->setVisible(false);
                            m_conversationWidget = 0;
                        } else {
                            useObject(object, false);
                        }
                    }
                }
            }
        }
    }
    // E picks up the hovered item if the player can carry it.
    static bool pickUpWasDown;
    if (m_inputs[InputKeyE] && !pickUpWasDown && m_hoveredGroundItem[0] >= 0) {
        Zone::ZoneItem *groundItem = m_world.item(m_hoveredGroundItem);
        if (groundItem) {
            if (m_player->canTakeItem(groundItem->item)) {
                Interaction pickUp;
                pickUp.position = m_hoveredGroundItem;
                pickUp.type = InteractionPickUp;
                m_player->m_interactions.push_back(pickUp);
            } else {
                m_chatWidget->printMessage(L"You can't carry more of these items.\n",
                                           plasma::Vector<4, float>(1.0f, 0.2f, 0.2f, 1.0f));
                playSound(SoundHumanVoice01, m_player->m_entityData.position, 1.0f, 1.0f);
            }
        }
    }
    // T calls the companion.
    static bool callWasDown;
    if (m_inputs[InputKeyT] && !callWasDown) {
        Interaction call;
        call.type = InteractionCallCompanion;
        m_player->m_interactions.push_back(call);
    }

    // Gliding pulls the player forward along its horizontal course and sideways by its bank (rotation
    // y); otherwise there is no extra acceleration.
    if (m_player->isGliding()) {
        plasma::Vector<3, float> course = m_player->m_entityData.velocity;
        course[2] = 0.0f;
        if (course.squaredLength() > 0.0f) {
            course.normalize();
            plasma::Vector<3, float> side = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f).cross(course);
            blend(m_player->m_entityData.acceleration, course * GliderAcceleration, milliseconds, 0.001f);
            m_player->m_entityData.acceleration -= side * m_player->m_entityData.rotation[1] * 0.05f;
        }
    } else {
        m_player->m_entityData.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    }
    // The look stick turns the camera like the mouse (cubed, so small deflections turn slowly).
    if (m_lookStick.squaredLength() != 0)
        handleMouseMove(pow(m_lookStick[0] * (1.0f / 32768.0f), 3.0f) * milliseconds,
                        -(pow(m_lookStick[1] * (1.0f / 32768.0f), 3.0f) * milliseconds));
    // The ring menu acts on a direction only after all were released once since it opened; W or S
    // close it.
    static bool ringMenuReleased;
    if (!m_ringMenuOpen)
        ringMenuReleased = false;
    if (m_inputs[InputKeyW] || m_inputs[InputKeyS])
        m_ringMenuOpen = false;

    // The path-following switch is never set.
    static bool followingPath;
    if (m_buildingMode) {
        // Build mode : R queues a build interaction.
        if (m_inputs[InputKeyR] && !interactWasDown) {
            Interaction build;
            build.type = InteractionBuild;
            m_player->m_interactions.push_back(build);
        }
    } else if (m_ringMenuOpen) {
        // The ring menu: A or the stick's left and D or its right step the selection (sound 85), repeating
        // every 200 ms while held; the selected stack's count and name show; R or E use it, unless its
        // level is above the player's (sound 50).
        static int leftRepeat;
        static int rightRepeat;
        bool left = m_inputs[InputKeyA] || m_moveStick[0] < 0;
        bool right = m_inputs[InputKeyD] || m_moveStick[0] > 0;
        if (!left)
            leftRepeat = 0;
        if (!right)
            rightRepeat = 0;
        if (!left && !right)
            ringMenuReleased = true;
        if (ringMenuReleased) {
            if (left && leftRepeat <= 0) {
                m_ringMenuSelection--;
                leftRepeat = 200;
                playSound(SoundMenuSelect, m_cameraPosition, 1.0f, 1.0f);
            }
            if (right && rightRepeat <= 0) {
                m_ringMenuSelection++;
                rightRepeat = 200;
                playSound(SoundMenuSelect, m_cameraPosition, 1.0f, 1.0f);
            }
            leftRepeat = std::max(leftRepeat - milliseconds, 0);
            rightRepeat = std::max(rightRepeat - milliseconds, 0);
        }
        std::vector<plasma::Vector<2, int>> menuItems = collectUsableItems();
        if (!menuItems.empty()) {
            // A negative selection wraps through the quick-item list collected above.
            unsigned int selected = m_ringMenuSelection;
            if ((int)selected < 0)
                selected =
                    (unsigned int)((selected + (-(int)selected / usable.size() + 1) * usable.size()) % usable.size());
            plasma::Vector<2, int> slot = menuItems[selected % menuItems.size()];
            std::wstringstream text;
            text << m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]].quantity;
            m_selector->setText(L"count", text.str(), true);
            text.str(L"");
            text << m_world.createItemName(m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]].item);
            text << " +" << m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]].item.power();
            m_selector->setText(L"name", text.str(), true);
            if ((m_inputs[InputKeyR] && !interactWasDown) || (m_inputs[InputKeyE] && !pickUpWasDown)) {
                if (m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]].item.level > m_player->m_entityData.level) {
                    playSound(SoundHumanVoice02, m_cameraPosition, 1.0f, 1.0f);
                } else {
                    useItem(&m_player->m_inventory.m_inventoryTabs[slot[0]][slot[1]]);
                    m_ringMenuOpen = false;
                }
            }
        } else {
            m_selector->setText(L"count", L"", true);
        }
    } else if (m_conversationWidget && m_conversationWidget->getNode()->isVisible() &&
               !m_conversationWidget->m_choices.empty()) {
        // 0x497f27: a conversation with choices holds the player still;
        // once its text is out, A and D (on their press) move the
        // highlight round the choices (sound 54).
        static bool choiceLeftWasDown;
        static bool choiceRightWasDown;
        if (m_conversationWidget->isDialogFinished()) {
            if (m_inputs[InputKeyA] && !choiceLeftWasDown) {
                SpeechWidget *conversation = m_conversationWidget;
                conversation->m_hoveredChoice =
                    (int)((conversation->m_hoveredChoice + conversation->m_choices.size() - 1) %
                          conversation->m_choices.size());
                playSound(SoundLever, m_cameraPosition, 1.0f, 1.0f);
            }
            if (m_inputs[InputKeyD] && !choiceRightWasDown) {
                m_conversationWidget->m_hoveredChoice =
                    (m_conversationWidget->m_hoveredChoice + 1) % m_conversationWidget->m_choices.size();
                playSound(SoundLever, m_cameraPosition, 1.0f, 1.0f);
            }
        }
        choiceLeftWasDown = m_inputs[InputKeyA];
        choiceRightWasDown = m_inputs[InputKeyD];
    } else if (!(m_chatWidget && m_chatWidget->m_typeboxActive) && m_player->m_entityData.stunTime <= 0) {
        // Path following: every 32 ms of the world clock the path search runs up to ten more rounds (until
        // the path is long or reaches its goal), then the player follows it, aiming at the next block's
        // centre.
        if (followingPath) {
            if ((m_world.getClock() - milliseconds) / 32 != m_world.getClock() / 32) {
                m_world.finishPathSearch(m_player);
                for (int i = 0; i < PathSearchRoundsPerUpdate; i++) {
                    if ((int)m_player->m_path.size() > PathSearchLengthLimit)
                        break;
                    if (!m_player->m_path.empty() &&
                        m_player->m_path.back() == plasma::Vector<3, int>(m_player->m_pathGoal))
                        break;
                    m_world.continuePathSearch(m_player);
                    m_world.finishPathSearch(m_player);
                }
            }
            m_player->m_entityData.setToggles((1 << CreatureToggleFaceAim), false);
            if (!m_world.followPath(m_player, milliseconds) && !m_player->m_path.empty()) {
                m_player->m_entityData.setToggles((1 << CreatureToggleFaceAim), true);
                m_player->m_entityData.attackRotation = plasma::Vector<3, float>(
                    plasma::Vector<3, Fixed>(m_player->m_path.back()) +
                    plasma::Vector<3, Fixed>(Fixed(0.5), Fixed(0.5), Fixed(0.5)) - m_player->m_entityData.position);
            }
        } else {
            // Otherwise the keys move the character unless a menu screen is open. Toggle bits: faceAim (4)
            // while an attack lasts, fastMovement (0x40) with Space while gliding with stamina left, else
            // while Shift is up; climbing (1) with Control.
            if (!m_startScreen->isVisible() && !m_characterCreationScreen->isVisible() &&
                !m_worldCreationScreen->isVisible() && !m_characterListScreen->isVisible() &&
                !m_connectScreen->isVisible() && !m_worldListScreen->isVisible())
                moveCharacter(milliseconds);
            m_player->m_entityData.setToggles((1 << CreatureToggleFaceAim), false);
            if (m_player->m_entityData.binaryToggles.gliding &&
                !(m_player->m_entityData.flags & (1 << CreatureFlagOnGround)))
                m_player->m_entityData.setToggles((1 << CreatureToggleFastMovement),
                                                  m_player->m_stamina > 0.0f ? m_inputs[InputKeySpace] : false);
            else
                m_player->m_entityData.setToggles((1 << CreatureToggleFastMovement), !m_inputs[InputKeyShift]);
            if (!m_inputs[InputKeyShift]) {
                unsigned char ability = m_player->m_entityData.currentAbility;
                if (ability != AbilityPresentItem && ability != AbilitySit && ability != AbilityRidePet &&
                    ability != AbilitySailBoat && ability != AbilityTeleportToCity && ability != AbilityOfferPetFood &&
                    ability != AbilityHealingStream && ability != AbilitySleep && ability != AbilityDrink &&
                    ability != AbilityEat && ability != AbilitySneak && ability != AbilitySwirlStrike) {
                    if (m_player->m_entityData.timeSinceAbility < m_player->attackDuration() + AimHoldTime ||
                        m_player->m_entityData.currentAbility == AbilityChargedShot ||
                        m_player->m_entityData.currentAbility == AbilitySalvo ||
                        m_player->m_entityData.currentAbility == AbilityChargeBoomerang ||
                        m_player->m_entityData.currentAbility == AbilityBlock ||
                        m_player->m_entityData.currentAbility == AbilityChargeFireball ||
                        m_player->m_entityData.currentAbility == AbilityMonsterCharge ||
                        m_player->m_entityData.currentAbility == AbilityCrush ||
                        m_player->m_entityData.currentAbility == AbilitySwirl ||
                        m_player->m_entityData.currentAbility == AbilitySupportRay ||
                        m_player->m_entityData.currentAbility == Ability35) {
                        if (m_player->m_entityData.currentAbility != AbilityFireExplosion &&
                            m_player->m_entityData.currentAbility != AbilityExplosion &&
                            m_player->m_entityData.currentAbility != AbilityLava &&
                            m_player->m_entityData.currentAbility != AbilitySummon)
                            m_player->m_entityData.setToggles((1 << CreatureToggleFaceAim), true);
                        m_player->m_entityData.setToggles((1 << CreatureToggleFastMovement), false);
                    }
                }
            }
            m_player->m_entityData.setToggles((1 << CreatureToggleClimbing), m_inputs[InputKeyControl]);
        }
    }

    // Picking. The aim point is where the cursor ray from the camera's focus meets the terrain (within
    // 60 blocks; 100 blocks out when it meets nothing). The cursor is the screen's centre: whatever
    // box projects onto all four sides of it is under the cursor, the nearest one to the camera wins.
    // Failing that, the nearest creature, object and item within four blocks of the player are picked.
    plasma::Vector<3, Fixed> aimPoint = m_cameraFocus + plasma::Vector<3, Fixed>(cursorRay * 100.0f);
    m_objectiveNode->setVisible(!m_world.getName().empty());
    float rayDistance = m_world.rayLength(m_cameraFocus, cursorRay, 60.0f, false, true);
    if (rayDistance >= 0.0f)
        aimPoint = m_cameraFocus + plasma::Vector<3, Fixed>(rayDistance * cursorRay);
    m_target = 0;
    unused8006d4 = 0;
    m_interactionCreatureId = 0;
    m_hoveredObject = plasma::Vector<3, int>(-1, -1, 0);
    m_hoveredGroundItem = plasma::Vector<3, int>(-1, -1, 0);
    long long closeCreatureId = 0;
    plasma::Vector<3, int> closeObject(-1, -1, 0);
    plasma::Vector<3, int> closeItem(-1, -1, 0);
    // Squared distances; negative while nothing is found.
    float hoverDistance = -1.0f;     // from the camera, of what is under the cursor
    float hostileDistance = -1.0f;   // from the camera, of a hostile creature under the cursor
    float closeDistance = -1.0f;     // from the player, of the nearest creature or object
    float closeItemDistance = -1.0f; // from the player, of the nearest item
    long long cursorCreatureId = 0;
    float cursorDistance =
        -1.0f; // from the player, of the creature under the cursor Full daylight at noon, none at midnight (the
    // day is 86,400,000 ms).
    float daylight = pow(1.0f - pow((float)m_world.getTime() * 2.0f / 86400000.0f - 1.0f, 4.0f), 5.0f);
    for (std::vector<NearbyCreature>::iterator it = nearby.begin(); it != nearby.end(); it++) {
        if (it->creature == m_player)
            continue;
        // A healing stream passes over enemies.
        if (m_player->m_entityData.currentAbility == AbilityHealingStream &&
            m_player->m_entityData.timeSinceAbility < m_player->attackDuration() &&
            (it->creature->m_entityData.hostilityType == HostilityAttackableObject ||
             it->creature->m_entityData.hostilityType == HostilityEnemy))
            continue;
        // In the dark a creature is seen only from nearer (down to a tenth of the range).
        float visibleRange = CreaturePickingRange;
        float light =
            m_world.cubeLight((int)it->creature->m_entityData.position[0], (int)it->creature->m_entityData.position[1],
                              (int)it->creature->m_entityData.position[2], 0, 0) /
            255.0f * daylight;
        if (light < 0.3f)
            visibleRange = light * 0.9f / 0.3f * CreaturePickingRange + 25.6f;
        if (!m_world.inRange(m_cameraPosition,
                             it->creature->m_drawnPosition + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                                 0.0f, 0.0f, -it->creature->m_stepOffset)),
                             false, visibleRange))
            continue;
        // The corners of the creature's box (its size plus a block, drawn where the creature is shown).
        bool left = false;
        bool below = false;
        bool right = false;
        bool above = false;
        for (int x = 0; x < 2; x++) {
            for (int y = 0; y < 2; y++) {
                for (int z = 0; z < 2; z++) {
                    plasma::Vector<3, Fixed> step(plasma::Vector<3, float>(0.0f, 0.0f, -it->creature->m_stepOffset));
                    float sizeZ = it->creature->m_entityData.appearance.size[2] + 1.0f;
                    float sizeY = it->creature->m_entityData.appearance.size[1] + 1.0f;
                    float sizeX = it->creature->m_entityData.appearance.size[0] + 1.0f;
                    plasma::Vector<3, float> view =
                        m_viewMatrix *
                        plasma::Vector<3, float>(it->creature->m_drawnPosition + step +
                                                 plasma::Vector<3, Fixed>(Fixed(x * sizeX - sizeX * 0.5f),
                                                                          Fixed(y * sizeY - sizeY * 0.5f),
                                                                          Fixed(z * sizeZ - sizeZ * 0.5f)) +
                                                 plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0)));
                    if (view[2] > 0.0f) {
                        m_projectionMatrix.transform(view);
                        if (view[0] < 0.0f)
                            left = true;
                        if (view[1] < 0.0f)
                            below = true;
                        if (view[0] > 0.0f)
                            right = true;
                        if (view[1] > 0.0f)
                            above = true;
                    }
                }
            }
        }
        float playerDistance = (float)(m_player->m_entityData.position - it->creature->m_drawnPosition).squaredLength();
        if (playerDistance < 16.0f && (closeDistance < 0.0f || closeDistance > playerDistance)) {
            closeDistance = playerDistance;
            closeCreatureId = it->creature->m_id;
        }
        if (left && below && right && above) {
            it->labelShown = true;
            float cameraDistance = (float)(m_cameraPosition - it->creature->m_drawnPosition).squaredLength();
            if (hoverDistance < 0.0f || hoverDistance > cameraDistance) {
                hoverDistance = cameraDistance;
                m_interactionCreatureId = it->creature->m_id;
            }
            if (m_world.isHostile(m_player, it->creature) &&
                (hostileDistance < 0.0f || hostileDistance > cameraDistance))
                hostileDistance = cameraDistance;
            if (cursorDistance < 0.0f || cursorDistance > playerDistance) {
                cursorDistance = playerDistance;
                cursorCreatureId = it->creature->m_id;
            }
        }
    }
    // The conversation partner stays selected.
    if (m_conversationWidget)
        m_interactionCreatureId = m_conversationWidget->m_creature->m_id;
    // The aim point moves onto a hostile creature under the cursor when it lies ahead of the player
    // (not while teleporting).
    if (hostileDistance >= 0.0f && m_player->m_entityData.currentAbility != AbilityTeleport) {
        plasma::Vector<3, Fixed> onTarget =
            m_cameraPosition + plasma::Vector<3, Fixed>(sqrt(hostileDistance) * cursorRay);
        if (cursorRay.dot(plasma::Vector<3, float>(onTarget - m_player->m_entityData.position)) > 0.0f)
            aimPoint = onTarget;
    }
    // A healing stream heals the creature under the cursor while it is being anticipated; Shift heals
    // the player.
    if (m_player->m_entityData.currentAbility == AbilityHealingStream && cursorCreatureId != 0 &&
        m_player->m_entityData.timeSinceAbility < m_player->anticipationTime())
        m_player->m_entityData.focusId = cursorCreatureId;
    if (m_inputs[InputKeyShift])
        m_player->m_entityData.focusId = m_player->m_id;
    // Abilities 30 to 33 take the aim point while anticipated and keep it.
    if (m_player->m_entityData.currentAbility == AbilityFireSwirl ||
        m_player->m_entityData.currentAbility == AbilityFireVortex ||
        m_player->m_entityData.currentAbility == AbilityWaterSwirl ||
        m_player->m_entityData.currentAbility == AbilityWaterVortex) {
        if (m_player->m_entityData.timeSinceAbility - milliseconds <= m_player->anticipationTime())
            m_player->m_abilityTarget = aimPoint;
        else
            aimPoint = m_player->m_abilityTarget;
    }

    // The objects of the 3 x 3 cells (8 blocks each) around the player; their box turns with them (odd
    // rotations swap x and y).
    int cellX = (int)m_player->m_entityData.position[0] / 8;
    int cellY = (int)m_player->m_entityData.position[1] / 8;
    for (int x = cellX - 1; x <= cellX + 1; x++) {
        for (int y = cellY - 1; y <= cellY + 1; y++) {
            std::list<Zone::CellRecord> *cell = m_world.getCellObjects(x, y);
            if (!cell)
                continue;
            for (std::list<Zone::CellRecord>::iterator it = cell->begin(); it != cell->end(); it++) {
                float playerDistance = (float)(m_player->m_entityData.position - it->object->position).squaredLength();
                if (playerDistance > 16.0f)
                    continue;
                float cameraDistance =
                    (float)(m_cameraPosition - it->object->position -
                            plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, it->object->size[2] * 0.5f)))
                        .squaredLength();
                if (closeDistance < 0.0f || closeDistance > playerDistance) {
                    closeDistance = playerDistance;
                    closeCreatureId = 0;
                    closeObject = it->id;
                }
                if (!(hoverDistance < 0.0f || hoverDistance > cameraDistance))
                    continue;
                if (!m_world.inRange(m_cameraPosition,
                                     it->object->position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                                0.0f, 0.0f, it->object->size[2] * 0.5f)),
                                     false, ObjectPickingRange))
                    continue;
                plasma::Vector<3, float> box = it->object->size;
                if (it->object->rotation % 2 != 0)
                    std::swap(box[0], box[1]);
                bool left = false;
                bool below = false;
                bool right = false;
                bool above = false;
                for (int boxX = 0; boxX < 2; boxX++) {
                    for (int boxY = 0; boxY < 2; boxY++) {
                        for (int boxZ = 0; boxZ < 2; boxZ++) {
                            plasma::Vector<3, float> view =
                                m_viewMatrix * plasma::Vector<3, float>(
                                                   it->object->position +
                                                   plasma::Vector<3, Fixed>(Fixed(boxX * box[0] - box[0] * 0.5f),
                                                                            Fixed(boxY * box[1] - box[1] * 0.5f),
                                                                            Fixed(boxZ * box[2])) +
                                                   plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0)));
                            if (view[2] > 0.0f) {
                                m_projectionMatrix.transform(view);
                                if (view[0] < 0.0f)
                                    left = true;
                                if (view[1] < 0.0f)
                                    below = true;
                                if (view[0] > 0.0f)
                                    right = true;
                                if (view[1] > 0.0f)
                                    above = true;
                            }
                        }
                    }
                }
                if (left && below && right && above) {
                    m_interactionCreatureId = 0;
                    hoverDistance = cameraDistance;
                    m_hoveredObject = it->id;
                }
            }
        }
    }
    // A dead player picks nothing.
    if (m_player->m_entityData.hp <= 0.0f) {
        m_interactionCreatureId = 0;
        m_hoveredObject = plasma::Vector<3, float>(-1.0f, -1.0f, 0.0f);
    }

    // The items on the ground of the 3 x 3 zones around the camera's zone, within four blocks of the
    // player; their box is their sprite's, placed as render draws them. Items the player cannot carry
    // rank as four blocks further.
    for (int x = m_cameraZone[0] - 1; x <= m_cameraZone[0] + 1; x++) {
        for (int y = m_cameraZone[1] - 1; y <= m_cameraZone[1] + 1; y++) {
            Zone *zone = m_world.zone(x, y);
            if (!zone)
                continue;
            int index = -1;
            for (std::vector<Zone::ZoneItem>::iterator it = zone->m_items.begin(); it != zone->m_items.end(); it++) {
                index++;
                if (!m_player->canUseFormula(it->item))
                    continue;
                float playerDistance =
                    plasma::Vector<3, float>(m_player->m_entityData.position - it->position).squaredLength();
                if (playerDistance > 16.0f)
                    continue;
                if (plasma::Vector<3, float>(m_player->m_entityData.position - it->position).squaredLength() >
                    ItemPickupRangeSquared)
                    continue;
                float cameraDistance = plasma::Vector<3, float>(m_cameraPosition - it->position).squaredLength();
                if (!m_player->canTakeItem(it->item)) {
                    cameraDistance += 16.0f;
                    playerDistance += 16.0f;
                }
                if (closeItemDistance < 0.0f || closeItemDistance > playerDistance) {
                    closeItemDistance = playerDistance;
                    closeCreatureId = 0;
                    closeObject = plasma::Vector<3, int>(-1, -1, 0);
                    closeItem = plasma::Vector<3, int>(x, y, index);
                }
                if (!(hoverDistance < 0.0f || hoverDistance > cameraDistance))
                    continue;
                if (!m_world.inRange(m_cameraPosition, it->position, false, ItemPickingRange))
                    continue;
                Sprite *sprite = m_world.m_spriteManager.itemSprite(&it->item);
                if (!sprite)
                    continue;
                plasma::Matrix<float> model;
                model.identity();
                model.translate((float)(it->position[0] + m_drawOriginX), (float)(it->position[1] + m_drawOriginY),
                                (float)it->position[2]);
                model.scale(it->scale, it->scale, it->scale);
                switch (it->item.category) {
                case ItemCategoryWeapon:
                case ItemCategoryChest:
                    model.rotateZ(it->rotation);
                    // Standing on end (the next case too).
                case ItemCategoryCoin:
                case ItemCategoryPlatinumCoin:
                    model.translate(0.0f, 0.0f, sprite->sizeX() * 0.5f);
                    model.rotateY(90.0f);
                    model.translate(sprite->sizeX() * -0.5f, sprite->sizeY() * -0.5f, sprite->sizeZ() * -0.5f);
                    break;
                default:
                    model.rotateZ(it->rotation);
                    model.translate(sprite->sizeX() * -0.5f, sprite->sizeY() * -0.5f, 0.0f);
                    break;
                }
                bool left = false;
                bool below = false;
                bool right = false;
                bool above = false;
                for (int boxX = 0; boxX < 2; boxX++) {
                    for (int boxY = 0; boxY < 2; boxY++) {
                        for (int boxZ = 0; boxZ < 2; boxZ++) {
                            plasma::Vector<3, float> view =
                                m_viewMatrix * (model * plasma::Vector<3, float>((float)(sprite->sizeX() * boxX),
                                                                                 (float)(sprite->sizeY() * boxY),
                                                                                 (float)(sprite->sizeZ() * boxZ)));
                            if (view[2] > 0.0f) {
                                m_projectionMatrix.transform(view);
                                if (view[0] < 0.0f)
                                    left = true;
                                if (view[1] < 0.0f)
                                    below = true;
                                if (view[0] > 0.0f)
                                    right = true;
                                if (view[1] > 0.0f)
                                    above = true;
                            }
                        }
                    }
                }
                if (left && below && right && above) {
                    hoverDistance = cameraDistance;
                    m_hoveredGroundItem = plasma::Vector<3, int>(x, y, index);
                    m_interactionCreatureId = 0;
                    m_hoveredObject = plasma::Vector<3, int>(-1, -1, 0);
                }
            }
        }
    }
    // Nothing under the cursor: what is nearest to the player.
    if (m_interactionCreatureId == 0 && m_hoveredObject[0] < 0) {
        if (closeCreatureId != 0)
            m_interactionCreatureId = closeCreatureId;
        if (closeObject[0] >= 0)
            m_hoveredObject = closeObject;
    }
    if (m_hoveredGroundItem[0] < 0 && closeItem[0] >= 0)
        m_hoveredGroundItem = closeItem;
    // The locked target takes the aim point when nearer than the hostile creature under the cursor
    // (m_target was cleared above).
    if (hostileDistance >= 0.0f && m_target &&
        hostileDistance * hostileDistance >
            plasma::Vector<3, float>(m_target->m_entityData.position - m_cameraFocus).squaredLength())
        aimPoint = m_cameraFocus +
                   plasma::Vector<3, Fixed>(
                       plasma::Vector<3, float>(m_target->m_entityData.position - m_cameraFocus).length() * cursorRay);

    // The creatures' life bars and names, rebuilt each frame under m_lifeBars, farthest first. A
    // creature is labelled when it is under the cursor (picking above), or with all bars shown (key V)
    // for players, or always for other players. A ridden mount and the player's own mount have none;
    // in the dark only nearer creatures.
    m_lifeBars->destroyChildren();
    for (std::vector<NearbyCreature>::reverse_iterator it = nearby.rbegin(); it != nearby.rend(); it++) {
        if (it->creature == m_player)
            continue;
        if (m_player->m_entityData.currentAbility == AbilityRidePet &&
            it->creature->m_entityData.ownerId == m_player->m_id)
            continue;
        float visibleRange = CreatureLabelRange;
        float light =
            m_world.cubeLight((int)it->creature->m_entityData.position[0], (int)it->creature->m_entityData.position[1],
                              (int)it->creature->m_entityData.position[2], 0, 0) /
            255.0f * daylight;
        if (light < 0.3f)
            visibleRange = light * 0.9f / 0.3f * CreatureLabelRange + 20.0f;
        if (!m_world.inRange(
                m_cameraPosition,
                it->creature->m_drawnPosition +
                    plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                        0.0f, 0.0f, it->creature->m_entityData.appearance.size[2] * 0.5f - it->creature->m_stepOffset)),
                true, visibleRange))
            continue;
        // The bar floats at the middle of the creature's height; a rider's a little higher (and 2 blocks
        // deeper in view space).
        plasma::Vector<3, float> view =
            m_viewMatrix *
            plasma::Vector<3, float>(
                it->creature->m_drawnPosition + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0)) +
                plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                    0.0f, 0.0f, it->creature->m_entityData.appearance.size[2] * 0.5f - it->creature->m_stepOffset)));
        if (it->creature->m_entityData.currentAbility == AbilitySailBoat ||
            it->creature->m_entityData.currentAbility == AbilityRidePet)
            view[2] += 2.0f;
        if (it->creature->m_entityData.ownerId != 0) {
            Creature *owner = m_world.creatureByID(it->creature->m_entityData.ownerId);
            if (owner && owner->m_entityData.currentAbility == AbilityRidePet)
                continue;
        }
        if (view[2] <= 0.0f)
            continue;
        m_projectionMatrix.transform(view);
        if (!it->labelShown && (view[0] < -1.0f || view[0] > 1.0f || view[1] < -1.0f || view[1] > 1.0f))
            continue;
        view *=
            0.5f * plasma::Vector<3, float>((float)m_engine->viewportWidth(), (float)-m_engine->viewportHeight(), 1.0f);
        view +=
            0.5f * plasma::Vector<3, float>((float)m_engine->viewportWidth(), (float)m_engine->viewportHeight(), 0.0f);
        if (view[1] < 50.0f)
            view[1] = 50.0f;
        // The bar template by hostility (an enemy's by whether it is peaceful). An unlabelled creature's
        // bar hides its name, unless all bars are shown.
        plasma::Node *bar;
        if (it->labelShown) {
            if (it->creature->m_entityData.hostilityType == HostilityEnemy)
                bar = (it->creature->isPeaceful() ? m_neutralLifeBarSmall : m_enemyLifeBarSmall)->instance(m_lifeBars);
            else if (it->creature->m_entityData.hostilityType == HostilityAttackableObject)
                bar = m_staticLifeBarSmall->instance(m_lifeBars);
            else
                bar = m_friendLifeBarSmall->instance(m_lifeBars);
        } else {
            if (!m_showAllLifeBars && it->creature->m_entityData.hostilityType != HostilityPlayer)
                continue;
            if (it->creature == m_player)
                continue;
            if (it->creature->m_entityData.hostilityType == HostilityEnemy)
                bar = (it->creature->isPeaceful() ? m_neutralLifeBarSmall : m_enemyLifeBarSmall)->instance(m_lifeBars);
            else if (it->creature->m_entityData.hostilityType == HostilityAttackableObject)
                bar = m_staticLifeBarSmall->instance(m_lifeBars);
            else
                bar = m_friendLifeBarSmall->instance(m_lifeBars);
            if (!m_showAllLifeBars) {
                for (std::list<plasma::Node *>::iterator child = bar->m_children.begin();
                     child != bar->m_children.end(); child++) {
                    if ((*child)->getName() == L"rarename") {
                        (*child)->setDisplay((*child)->display()->clone());
                        (*child)->setVisible(false);
                    }
                }
            } else {
                for (std::list<plasma::Node *>::iterator child = bar->m_children.begin();
                     child != bar->m_children.end(); child++) {
                    if ((*child)->getName() == L"rarename")
                        (*child)->setVisible(true);
                }
            }
        }
        // The instance gets its own transformation, display and texts.
        bar->setTransformation(bar->transformation()->clone());
        bar->setDisplay(bar->display()->clone());
        std::list<plasma::Node *> texts;
        bar->findNodes(texts, L"info");
        bar->findNodes(texts, L"rarename");
        bar->findNodes(texts, L"text");
        for (std::list<plasma::Node *>::iterator text = texts.begin(); text != texts.end(); text++) {
            if ((*text)->getShape())
                (*text)->setShape((*text)->getShape()->clone());
        }
        plasma::Node *fill = bar->findNode(L"bar");
        fill->setTransformation(fill->transformation()->clone());
        plasma::Node *missing = bar->findNode(L"bar2");
        missing->setTransformation(missing->transformation()->clone());
        bar->transformation()->m_translation =
            plasma::Vector<2, float>(view[0], view[1]) - bar->transformation()->m_pivot.value();
        bar->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        // The name: the creature's own, else one generated from its id for rare creatures and friendly
        // ones, else none.
        if (strlen(it->creature->m_entityData.name) != 0)
            bar->setText(L"rarename", std::string(it->creature->m_entityData.name), true);
        else if (it->creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagTinted))
            bar->setText(L"rarename",
                         m_world.randomName((unsigned int)it->creature->m_id, it->creature->m_entityData.race), true);
        else if (it->creature->m_entityData.hostilityType == HostilityFriendlyNpc)
            bar->setText(L"rarename",
                         m_world.randomName((unsigned int)it->creature->m_id, it->creature->m_entityData.race), true);
        else
            bar->setText(L"rarename", L"", true);
        // The info line's colour: blue for players, grey for objects, by the level difference for the rest
        // (red, orange, cyan, else white).
        std::wstringstream text;
        std::list<plasma::Node *> infos;
        bar->findNodes(infos, L"info");
        plasma::Vector<4, float> color(1.0f, 1.0f, 1.0f, 1.0f);
        if (it->creature->m_entityData.hostilityType == HostilityPlayer)
            color = plasma::Vector<4, float>(0.0f, 0.8f, 1.0f, 1.0f);
        else if (it->creature->m_entityData.hostilityType == HostilityAttackableObject)
            color = plasma::Vector<4, float>(0.8f, 0.8f, 0.8f, 1.0f);
        else if (levelBonus((float)it->creature->m_entityData.level) >
                 levelBonus((float)m_player->m_entityData.level) + 0.3f)
            color = plasma::Vector<4, float>(1.0f, 0.2f, 0.2f, 1.0f);
        else if (levelBonus((float)it->creature->m_entityData.level) >
                 levelBonus((float)m_player->m_entityData.level) + 0.1f)
            color = plasma::Vector<4, float>(1.0f, 0.6f, 0.2f, 1.0f);
        else if (levelBonus((float)it->creature->m_entityData.level) >
                 levelBonus((float)m_player->m_entityData.level) - 0.1f)
            color = plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f);
        for (std::list<plasma::Node *>::iterator info = infos.begin(); info != infos.end(); info++) {
            plasma::TextShape *shape = textShape((*info)->getShape());
            if (shape) {
                shape->m_color = color;
                shape->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
            }
        }
        // "<race>" and the creature's rank ("+n").
        text << m_world.m_speech.getForm(m_world.creatureKey(it->creature->m_entityData.race), L"singular");
        if (it->creature->m_entityData.rarity)
            text << " +" << (int)it->creature->m_entityData.rarity << std::endl;
        bar->setText(L"info", text.str(), true);
        text.str(L"");
        text << (int)it->creature->m_entityData.hp << L"/" << (int)it->creature->maxHP();
        bar->setText(L"text", text.str(), true);
        // The bar fills with the hit points; bar2 shows the missing part.
        fill->transformation()->m_deformation.value().identity();
        fill->transformation()->m_deformation.value().scale(it->creature->m_entityData.hp / it->creature->maxHP(), 1.0f,
                                                            1.0f);
        fill->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
        missing->transformation()->m_deformation.value().identity();
        missing->transformation()->m_deformation.value().scale(
            1.0f - it->creature->m_entityData.hp / it->creature->maxHP(), 1.0f, 1.0f);
        missing->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }

    // Build mode moves the piece (EntityData::attackRotation) with the movement keys turned by the
    // camera; otherwise the attack direction: along the creature's heading while Shift is held, else
    // towards the aim point.
    if (m_buildingMode) {
        plasma::Matrix<float> yaw;
        yaw.identity();
        yaw.rotateZ(m_cameraRotation[2]);
        if (m_inputs[InputKeyW])
            m_player->m_entityData.attackRotation +=
                milliseconds * 0.005f * yaw.transformDirection(plasma::Vector<3, float>(0.0f, -1.0f, 0.0f));
        if (m_inputs[InputKeyS])
            m_player->m_entityData.attackRotation +=
                milliseconds * 0.005f * yaw.transformDirection(plasma::Vector<3, float>(0.0f, 1.0f, 0.0f));
        if (m_inputs[InputKeyA])
            m_player->m_entityData.attackRotation +=
                milliseconds * 0.005f * yaw.transformDirection(plasma::Vector<3, float>(1.0f, 0.0f, 0.0f));
        if (m_inputs[InputKeyD])
            m_player->m_entityData.attackRotation +=
                milliseconds * 0.005f * yaw.transformDirection(plasma::Vector<3, float>(-1.0f, 0.0f, 0.0f));
        if (m_inputs[InputKeyControl])
            m_player->m_entityData.attackRotation +=
                milliseconds * 0.005f * yaw.transformDirection(plasma::Vector<3, float>(0.0f, 0.0f, -1.0f));
        if (m_inputs[InputKeySpace])
            m_player->m_entityData.attackRotation +=
                milliseconds * 0.005f * yaw.transformDirection(plasma::Vector<3, float>(0.0f, 0.0f, 1.0f));
    } else if (!followingPath) {
        if (m_inputs[InputKeyShift]) {
            plasma::Matrix<float> yaw;
            yaw.identity();
            yaw.rotateZ(m_player->m_entityData.rotation[2]);
            m_player->m_entityData.attackRotation = yaw.transformDirection(plasma::Vector<3, float>(0.0f, 1.0f, 0.0f));
        } else {
            m_player->m_entityData.attackRotation =
                plasma::Vector<3, float>(aimPoint - m_player->m_entityData.position);
        }
    }
    interactWasDown = m_inputs[InputKeyR];
    pickUpWasDown = m_inputs[InputKeyE];
    callWasDown = m_inputs[InputKeyT];

    // The attacks and skills (not while the pointer shows, nor in build mode). The teleport input
    // starts Teleport to City on its press. Each bound skill of the bar (the right button, then keys 1
    // to 4) starts while its input is held, and a held skill ends with its release form when let go.
    // Without a skill input the left button attacks.
    if (!cursorShown && !m_buildingMode) {
        bool skillInput = false;
        static bool teleportToCityWasDown;
        if (m_inputs[InputTeleportToCity] && !teleportToCityWasDown &&
            (m_player->m_entityData.currentAbility == AbilityNone ||
             m_player->m_entityData.timeSinceAbility > m_player->attackDuration())) {
            m_player->m_entityData.currentAbility = AbilityTeleportToCity;
            m_player->m_entityData.timeSinceAbility = 0;
        }
        teleportToCityWasDown = m_inputs[InputTeleportToCity];
        for (int i = 1; i < (int)m_barAbilities.size(); i++) {
            if (m_barAbilities[i] == AbilityNone)
                continue;
            if (m_inputs[i]) {
                skillInput = true;
                // The right button's skill only when canCancelAbility allows.
                if (!m_player->canCancelAbility() && i - 1 <= 0)
                    continue;
                if (!m_player->canUseSkill(m_barAbilities[i]))
                    continue;
                // Pet food in the pet slot goes back to the inventory.
                if (m_player->m_entityData.equipment.pet.category == ItemCategoryPetFood) {
                    m_player->m_inventory.add(m_player->m_entityData.equipment.pet, -1);
                    m_player->m_entityData.equipment.pet = Item();
                }
                switch (m_barAbilities[i]) {
                case Ability3:
                case Ability4:
                case AbilityChargedShotRelease:
                case AbilityFireVortex:
                case AbilityWaterVortex:
                case AbilityFireball:
                case AbilityWaterSalvo:
                case AbilityFireSalvo:
                case AbilityTwoHandedSpin:
                case AbilityTwoHandedSlam:
                case AbilityFireBeam:
                case AbilityFireRay:
                    m_player->performSpecialAttack();
                    break;
                case AbilityHealingStream:
                    if (m_player->m_entityData.timeSinceAbility >= m_player->attackDuration()) {
                        m_player->m_entityData.currentAbility = AbilityHealingStream;
                        m_player->m_entityData.timeSinceAbility = 0;
                        m_player->m_entityData.focusId = m_player->m_id;
                    }
                    break;
                case AbilityBulwark:
                    m_world.applyBuffSkill(m_player, AbilityBulwark, &updates);
                    playSound(SoundBulwark, m_player->m_entityData.position, 1.0f, 1.0f);
                    break;
                case AbilityWarFrenzy:
                    m_world.applyBuffSkill(m_player, AbilityWarFrenzy, &updates);
                    break;
                case AbilityManaShield:
                    m_world.applyBuffSkill(m_player, AbilityManaShield, &updates);
                    playSound(SoundManashield, m_player->m_entityData.position, 1.0f, 1.0f);
                    break;
                case AbilitySwiftness:
                    m_world.applyBuffSkill(m_player, AbilitySwiftness, &updates);
                    break;
                case AbilityCamouflage:
                    m_world.applyBuffSkill(m_player, AbilityCamouflage, &updates);
                    break;
                case AbilitySmash:
                    // A leap along twice the aim, its duration from the distance (50 blocks a second), at most 50
                    // blocks, 15 up.
                    m_player->m_entityData.currentAbility = (unsigned char)m_barAbilities[i];
                    m_player->m_entityData.velocity = m_player->m_entityData.attackRotation * 2.0f;
                    m_player->m_leapDuration = (int)(m_player->m_entityData.velocity.length() / 50.0f * 1000.0f);
                    if (m_player->m_entityData.velocity.squaredLength() > 2500.0f) {
                        m_player->m_entityData.velocity.normalize();
                        m_player->m_entityData.velocity *= 50.0f;
                    }
                    m_player->m_entityData.velocity[2] = 15.0f;
                    m_player->m_entityData.timeSinceAbility = 0;
                    m_player->m_entityData.rollTime = 0;
                    break;
                case AbilityAmbush:
                    if (m_player->m_entityData.mp > 0.0f) {
                        m_player->m_entityData.currentAbility = AbilityAmbush;
                        m_player->m_entityData.timeSinceAbility = 0;
                        m_player->m_entityData.rollTime = 0;
                    }
                    break;
                case AbilityBlockJab:
                    if (m_player->m_entityData.currentAbility != AbilityBlockJab) {
                        m_player->m_entityData.currentAbility = AbilityBlockJab;
                        m_player->m_entityData.timeSinceAbility = 0;
                        m_player->m_entityData.rollTime = 0;
                    }
                    break;
                case AbilityPerforate:
                    if (m_player->m_entityData.mp > 0.0f) {
                        m_player->m_entityData.currentAbility = AbilityPerforate;
                        m_player->m_entityData.timeSinceAbility = 0;
                        m_player->m_entityData.rollTime = 0;
                    }
                    break;
                case AbilityKick:
                    if (m_player->m_entityData.mp > 0.0f) {
                        m_player->m_entityData.currentAbility = AbilityKick;
                        m_player->m_entityData.timeSinceAbility = 0;
                        m_player->m_entityData.rollTime = 0;
                    }
                    break;
                case AbilitySwirlStrike:
                    m_player->m_entityData.currentAbility = AbilitySwirlStrike;
                    m_player->m_entityData.timeSinceAbility = 0;
                    m_player->m_entityData.rollTime = 0;
                    break;
                case AbilityBlock:
                case AbilityChargedShot:
                case AbilitySalvo:
                case AbilityChargeBoomerang:
                case AbilityCrush:
                case AbilitySwirl:
                case AbilityMonsterCharge:
                    // Held skills start once.
                    if (m_player->m_entityData.currentAbility != m_barAbilities[i]) {
                        m_player->m_entityData.currentAbility = (unsigned char)m_barAbilities[i];
                        m_player->m_entityData.timeSinceAbility = 0;
                        m_player->m_entityData.rollTime = 0;
                    }
                    break;
                case AbilitySupportRay:
                    if (m_player->m_entityData.mp > 0.0f &&
                        m_player->m_entityData.currentAbility != m_barAbilities[i]) {
                        m_player->m_entityData.currentAbility = (unsigned char)m_barAbilities[i];
                        m_player->m_entityData.timeSinceAbility = 0;
                    }
                    break;
                case AbilityIntercept:
                    // A dash along the aim at 50 blocks a second, at most 5 up.
                    m_player->m_entityData.currentAbility = (unsigned char)m_barAbilities[i];
                    m_player->m_entityData.velocity = m_player->m_entityData.attackRotation;
                    m_player->m_leapDuration = (int)(m_player->m_entityData.velocity.length() / 50.0f * 1000.0f);
                    if (m_player->m_entityData.velocity.squaredLength() > 0.0f) {
                        m_player->m_entityData.velocity.normalize();
                        m_player->m_entityData.velocity *= 50.0f;
                        if (m_player->m_entityData.velocity[2] > 5.0f)
                            m_player->m_entityData.velocity[2] = 5.0f;
                    }
                    m_player->m_entityData.timeSinceAbility = 0;
                    m_player->m_entityData.rollTime = 0;
                    break;
                case AbilityRetreat:
                    // A jump back from the aim, faster with the skill.
                    m_player->m_entityData.retreat = -m_player->m_entityData.attackRotation;
                    m_player->m_entityData.retreat[2] = 0.0f;
                    if (m_player->m_entityData.retreat.squaredLength() > 0.0f) {
                        m_player->m_entityData.retreat.normalize();
                        m_player->m_entityData.retreat *=
                            RetreatSpeed + RetreatSkillSpeed * m_player->abilitySkillBonus(AbilityRetreat, -1);
                        m_player->m_entityData.retreat[2] =
                            RetreatRise + RetreatSkillRise * m_player->abilitySkillBonus(AbilityRetreat, -1);
                    }
                    m_player->m_skillCooldowns[AbilityRetreat] = m_player->cooldown(AbilityRetreat, -1);
                    break;
                case AbilityAim:
                    m_player->m_entityData.setToggles((1 << CreatureToggleSneaking), true);
                    m_player->m_skillCooldowns[AbilityAim] = m_player->cooldown(AbilityAim, -1);
                    break;
                case AbilityShuriken:
                    // A roll away from the aim at 30, 20 up, for 600 ms.
                    m_player->m_entityData.currentAbility = AbilityShuriken;
                    m_player->m_entityData.velocity = -m_player->m_entityData.attackRotation;
                    m_player->m_entityData.velocity[2] = 0.0f;
                    if (m_player->m_entityData.velocity.squaredLength() > 0.0f) {
                        m_player->m_entityData.velocity.normalize();
                        m_player->m_entityData.velocity *= 30.0f;
                        m_player->m_entityData.velocity[2] = 20.0f;
                    }
                    m_player->m_entityData.timeSinceAbility = 0;
                    m_player->m_entityData.rollTime = RollDurationMs;
                    break;
                default:
                    if (m_player->m_entityData.mp >= m_player->manaCost(m_barAbilities[i], -1)) {
                        m_player->m_entityData.currentAbility = (unsigned char)m_barAbilities[i];
                        m_player->m_entityData.timeSinceAbility = 0;
                        m_player->m_entityData.rollTime = 0;
                    }
                    break;
                }
            } else if (m_barAbilities[i] == m_player->m_entityData.currentAbility) {
                // The input is released while its held skill runs: the release form.
                switch (m_barAbilities[i]) {
                case AbilityCrush:
                    if (m_player->m_entityData.specialization == SpecializationGuardian) {
                        m_player->m_entityData.currentAbility = AbilityGuardianCrushStrike;
                        m_player->m_entityData.timeSinceAbility = 0;
                    } else {
                        m_player->m_entityData.currentAbility = AbilityCrushStrike;
                        m_player->m_entityData.timeSinceAbility = m_player->anticipationTime();
                    }
                    break;
                case AbilitySwirl:
                    m_player->m_entityData.currentAbility = AbilitySwirlStrike;
                    m_player->m_entityData.timeSinceAbility = m_player->anticipationTime();
                    break;
                case AbilityMonsterCharge:
                    m_player->m_entityData.currentAbility = AbilityMonsterChargeStrike;
                    m_player->m_entityData.timeSinceAbility = m_player->anticipationTime();
                    break;
                case AbilityChargedShot:
                    m_player->m_entityData.currentAbility = AbilityChargedShotRelease;
                    m_player->m_entityData.timeSinceAbility = 0;
                    break;
                case AbilitySalvo:
                    m_player->m_entityData.currentAbility = AbilitySalvoRelease;
                    m_player->m_entityData.timeSinceAbility = 0;
                    break;
                case AbilityChargeBoomerang:
                    m_player->m_entityData.currentAbility = AbilityThrowBoomerang;
                    m_player->m_entityData.timeSinceAbility = 0;
                    break;
                case AbilityChargeFireball:
                    m_player->m_entityData.currentAbility = AbilityFireball;
                    m_player->m_entityData.timeSinceAbility = 0;
                    break;
                case AbilityBlock:
                    m_player->m_entityData.currentAbility = AbilityBlockStrike;
                    m_player->m_entityData.timeSinceAbility = 0;
                    break;
                case Ability35:
                    m_player->m_entityData.currentAbility = AbilityNone;
                    m_player->m_entityData.timeSinceAbility = 0;
                    break;
                case AbilityBlockJab:
                    if (m_player->m_entityData.timeSinceAbility > m_player->attackDuration()) {
                        m_player->m_entityData.currentAbility = Ability3;
                        m_player->m_entityData.timeSinceAbility = m_player->attackDuration();
                    }
                    break;
                }
            }
        }
        // The left button's basic attack (not while intercepting), which also returns pet food from the
        // pet slot.
        if (!skillInput && m_player->m_entityData.currentAbility != AbilityIntercept && m_inputs[InputMouseLeft]) {
            m_player->performAttack();
            if (m_player->m_entityData.equipment.pet.category == ItemCategoryPetFood) {
                m_player->m_inventory.add(m_player->m_entityData.equipment.pet, -1);
                m_player->m_entityData.equipment.pet = Item();
            }
        }
        // A support ray on the right button and ability 29 on the left end with their button.
        if (!m_inputs[InputMouseRight] && m_barAbilities[AbilityBarMouseRight] == AbilitySupportRay &&
            m_player->m_entityData.currentAbility == AbilitySupportRay)
            m_player->m_entityData.currentAbility = AbilityNone;
        if (!m_inputs[InputMouseLeft] && m_barAbilities[AbilityBarMouseLeft] == Ability29 &&
            m_player->m_entityData.currentAbility == Ability29)
            m_player->m_entityData.currentAbility = AbilityNone;
        if (skillInput)
            m_player->m_entityData.setToggles((1 << CreatureToggleClimbing), false);
    }

    // Connected, the player's own hits, buffs and projectiles go to the server; the local server takes
    // the whole frame.
    if (m_socket) {
        for (std::list<HitPacket>::iterator it = updates.hits.begin(); it != updates.hits.end(); it++)
            if (it->attackerId == m_player->m_id)
                m_hits.push_back(*it);
        for (std::list<BuffPacket>::iterator it = updates.buffs.begin(); it != updates.buffs.end(); it++)
            if (it->sourceId == m_player->m_id)
                m_buffs.push_back(*it);
        for (std::list<BulletPacket>::iterator it = updates.projectiles.begin(); it != updates.projectiles.end(); it++)
            if (it->ownerId == m_player->m_id)
                m_projectiles.push_back(*it);
    }
    if (m_localServer)
        m_localServer->addServerUpdates(updates);

    // Unless the player sits, sleeps, rides, sneaks, holds a ray or the right button, an ability ends
    // after ten seconds; holding pet food, the player offers it (ability 82) half a second after the
    // attack.
    unsigned char current = m_player->m_entityData.currentAbility;
    if (current != AbilitySit && current != AbilityRidePet && current != AbilitySailBoat &&
        current != AbilityOfferPetFood && current != AbilitySleep && current != AbilityBlock && current != Ability35 &&
        current != AbilitySneak && current != AbilitySupportRay && current != Ability29 &&
        current != AbilityChargeFireball && !m_inputs[InputMouseRight]) {
        if (m_player->m_entityData.equipment.pet.category == ItemCategoryPetFood &&
            m_player->m_entityData.timeSinceAbility > m_player->attackDuration() + 500)
            m_player->m_entityData.currentAbility = AbilityOfferPetFood;
        else if (m_player->m_entityData.timeSinceAbility > 10000)
            m_player->m_entityData.currentAbility = AbilityNone;
    }

    // The camera: its focus (m_cameraFocus) snaps to the player beyond 50 blocks and otherwise follows
    // it (in the menus it rests 50 blocks over the spawn point); eight probes around the camera
    // shorten its distance at solid blocks, and a camera left inside the terrain is moved along a ray
    // from the player. Then the matrices, with the shake.
    plasma::Matrix<float> orientation;
    orientation.identity();
    orientation.rotateZ(m_cameraRotation[2]);
    orientation.rotateY(m_cameraRotation[1]);
    orientation.rotateX(m_cameraRotation[0]);
    if (plasma::Vector<3, float>(m_cameraFocus - m_player->m_entityData.position).squaredLength() > 2500.0f)
        m_cameraFocus = m_player->m_entityData.position;
    if (!m_startScreen->isVisible() && !m_characterListScreen->isVisible() && !m_connectScreen->isVisible() &&
        !m_worldListScreen->isVisible()) {
        float factor = 0.0f;
        blend(factor, 1.0f, milliseconds, 0.025f);
        m_cameraFocus[0] += Fixed(factor) * (m_player->m_entityData.position[0] - m_cameraFocus[0]);
        m_cameraFocus[1] += Fixed(factor) * (m_player->m_entityData.position[1] - m_cameraFocus[1]);
        Fixed height = m_player->m_entityData.position[2] - Fixed(m_player->m_stepOffset);
        if (!m_characterCreationScreen->isVisible() && !m_worldCreationScreen->isVisible()) {
            unsigned char ability = m_player->m_entityData.currentAbility;
            if (ability == AbilitySleep || ability == AbilitySit) {
                height += Fixed(m_player->m_entityData.appearance.size[0] * 0.5f);
            } else {
                height += Fixed(m_player->m_entityData.appearance.size[2] * 0.5f);
                height += Fixed(ability == AbilityRidePet || ability == AbilitySailBoat ? 1.5f : 0.5f);
            }
        }
        blend(m_cameraFocus[2], height, milliseconds, 0.005f);
    } else {
        m_cameraFocus = plasma::Vector<3, Fixed>(m_world.spawnPoint());
        Field *field = m_world.getField((int)m_cameraFocus[0], (int)m_cameraFocus[1]);
        if (field)
            m_cameraFocus[2] = field->m_baseZ + field->m_size;
        m_cameraFocus[2] += Fixed(50);
    }
    m_cameraPosition = m_cameraFocus;
    plasma::Vector<3, float> cameraDirection =
        orientation.transformDirection(plasma::Vector<3, float>(0.0f, 0.0f, 1.0f));
    m_cameraPosition = m_cameraPosition - plasma::Vector<3, Fixed>(m_targetCameraDistance * cameraDirection);
    // The camera comes nearer while a corner of the half-block cube around it (corners 0.25 blocks
    // out) is in a solid block.
    float cameraDistance = m_targetCameraDistance;
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++) {
            for (int z = 0; z < 2; z++) {
                plasma::Vector<3, Fixed> probe =
                    m_cameraPosition +
                    plasma::Vector<3, Fixed>(Fixed(x - 0.5), Fixed(y - 0.5), Fixed(z - 0.5)) * Fixed(0.5);
                if (m_world.getBlock(probe[0], probe[1], probe[2], 0).isSolid()) {
                    float shortened = m_targetCameraDistance -
                                      m_world.rayLength(probe, cameraDirection, m_targetCameraDistance, true, true);
                    if (shortened < cameraDistance)
                        cameraDistance = shortened;
                }
            }
        }
    }
    m_targetCameraDistance = cameraDistance;
    m_cameraPosition = m_cameraFocus - plasma::Vector<3, Fixed>(cameraDistance * cameraDirection);
    m_actualCameraDistance = m_targetCameraDistance;
    if (m_world.getBlock(m_cameraPosition[0], m_cameraPosition[1], m_cameraPosition[2], 0).isSolid()) {
        plasma::Vector<3, float> fromPlayer(m_cameraPosition - m_player->m_entityData.position);
        if (fromPlayer.squaredLength() > 0.0f) {
            fromPlayer.normalize();
            float length =
                m_world.rayLength(m_player->m_entityData.position, fromPlayer, CameraEscapeRayLength, true, true);
            if (length >= 0.0f)
                m_cameraPosition = m_player->m_entityData.position + plasma::Vector<3, Fixed>(length * fromPlayer);
        }
    }
    m_cameraMatrix.identity();
    m_cameraMatrix.translate(plasma::Vector<3, float>(m_cameraPosition));
    m_cameraMatrix.rotateZ(m_cameraRotation[2]);
    m_cameraMatrix.rotateY(m_cameraRotation[1]);
    m_cameraMatrix.rotateX(m_cameraRotation[0]);
    m_worldViewMatrix.identity();
    m_worldViewMatrix.rotateX(-m_cameraRotation[0]);
    m_worldViewMatrix.rotateY(-m_cameraRotation[1]);
    m_worldViewMatrix.rotateZ(-m_cameraRotation[2]);
    m_worldViewMatrix.translate(-plasma::Vector<3, float>(m_cameraPosition));
    // The drawing origin is the camera's focus, not the camera.
    m_drawOriginX = -m_cameraFocus[0];
    m_drawOriginY = -m_cameraFocus[1];
    // The shake: half its amount, along two cosines of the world clock (0.03 and 0.04 radians per
    // millisecond).
    m_viewMatrix.identity();
    float halfShake = m_screenShake * 0.5f;
    m_viewMatrix.translate((float)cos(m_world.getClock() * 0.03f) * halfShake,
                           (float)cos(m_world.getClock() * 0.04f) * halfShake);
    m_viewMatrix.rotateX(-m_cameraRotation[0]);
    m_viewMatrix.rotateY(-m_cameraRotation[1]);
    m_viewMatrix.rotateZ(-m_cameraRotation[2]);
    m_viewMatrix.translate(
        plasma::Vector<3, float>(m_cameraPosition + plasma::Vector<3, Fixed>(m_drawOriginX, m_drawOriginY, Fixed(0))) *
        -1.0f);
    blend(m_screenShake, 0.0f, milliseconds, 0.005f);

    // The streaming requests for the worker threads: the chunk window around the camera, the camera's
    // block and zone, the zones of the local players (every player with a local server) and those of
    // them, and of their regions, that still have no data (only asked of a server).
    EnterCriticalSection(&m_streamingLock);
    // Convert through float: far from the origin this rounds to whole blocks, whereas an integer conversion of Fixed would truncate.
    m_chunkOrigin =
        plasma::Vector<2, int>(plasma::Vector<2, float>((float)(m_cameraPosition[0] / 32 - m_viewDistance / 2),
                                                        (float)(m_cameraPosition[1] / 32 - m_viewDistance / 2)));
    m_cameraBlock =
        plasma::Vector<2, int>(plasma::Vector<2, float>((float)m_cameraPosition[0], (float)m_cameraPosition[1]));
    m_playerZones.clear();
    if (m_localServer) {
        for (std::map<long long, Creature *>::iterator it = m_world.m_creatures.begin();
             it != m_world.m_creatures.end(); it++) {
            Creature *creature = it->second;
            if (creature && creature->m_entityData.hostilityType == HostilityPlayer)
                m_playerZones.push_back(
                    plasma::Vector<2, int>((int)creature->m_entityData.position[0] / BlocksPerZone,
                                           (int)creature->m_entityData.position[1] / BlocksPerZone));
        }
    } else {
        m_playerZones.push_back(plasma::Vector<2, int>((int)m_player->m_entityData.position[0] / BlocksPerZone,
                                                       (int)m_player->m_entityData.position[1] / BlocksPerZone));
    }
    m_zoneRequests.clear();
    m_regionRequests.clear();
    for (std::list<plasma::Vector<2, int>>::iterator it = m_playerZones.begin(); it != m_playerZones.end(); it++) {
        Zone *zone = m_world.zone((*it)[0], (*it)[1]);
        if (zone && zone->m_needsServerUpdate)
            m_zoneRequests.push_back(*it);
        Region *region = m_world.getRegion((*it)[0] / ZonesPerRegion, (*it)[1] / ZonesPerRegion);
        if (region && region->m_needsServerUpdate)
            m_regionRequests.push_back(plasma::Vector<2, int>((*it)[0] / ZonesPerRegion, (*it)[1] / ZonesPerRegion));
    }
    m_cameraZone = plasma::Vector<2, int>(plasma::Vector<2, float>((float)(m_cameraPosition[0] / BlocksPerZone),
                                                                   (float)(m_cameraPosition[1] / BlocksPerZone)));
    if (!m_socket) {
        m_zoneRequests.clear();
        m_regionRequests.clear();
    }
    LeaveCriticalSection(&m_streamingLock);
    m_world.leaveRegions();
    m_engine->advance(milliseconds);
}

// Rebuilds the character list: a preview per saved character and one more, empty, for a new
// character (updateWorldList's counterpart).
void GameController::refreshCharacterList()
// Rows of 600 x 220 pixels, 230 apart from y 50, at x 300.
{
    m_selectCharacterButton->setParent(0);
    m_characterMenuButton->setParent(0);
    m_deleteCharacterButton->setParent(0);
    m_characterListScreen->destroyChildren();
    m_characterRows.clear();
    for (int i = 0; i < (int)m_characters.size() + 1; ++i) {
        plasma::Node *node = m_blackWidget->instance(m_characterListScreen);
        node->setTransformation(node->transformation()->clone());
        node->setDisplay(node->display()->clone());
        node->widget()->resizeTo(600.0f, 220.0f, true);
        plasma::Node *previewNode = m_engine->createNode(0, 0, 0, 0);
        Creature *character = 0;
        if (i < (int)m_characters.size())
            character = m_characters[i];
        CharacterPreviewWidget *preview = new CharacterPreviewWidget(m_engine, previewNode, this);
        preview->m_creature = character;
        node->moveIntoWidget(m_blackWidget->getName(), preview, true);
        node->widget()->moveTo(300.0f, (float)(50 + i * 230), true);
        m_characterRows.push_back(node);
    }
    m_characterListScreen->initializeWidgets();
    m_selectCharacterButton->setParent(m_characterListScreen);
    m_characterMenuButton->setParent(m_characterListScreen);
    m_deleteCharacterButton->setParent(m_characterListScreen);
}

// Append a quad's two triangles as (0, 1, 2, 0, 2, 3) plus the first vertex index.
static void addQuad(std::vector<int> &indices, size_t first)
{
    indices.push_back(first);
    indices.push_back((int)(first + 1));
    indices.push_back((int)(first + 2));
    indices.push_back(first);
    indices.push_back((int)(first + 2));
    indices.push_back((int)(first + 3));
}

// Whether a block of the type shows its face towards a neighbour: towards the open, and towards
// water unless the block is water itself.
static inline bool isFaceOpen(unsigned char type, int neighbourType)
{
    return neighbourType == BlockAir || (type != BlockWater && neighbourType == BlockWater);
}

void GameController::updateChunk(int x, int y)
{
    if (x < 0 || y < 0 || x * BlocksPerChunk >= BlocksPerWorld || y * BlocksPerChunk >= BlocksPerWorld)
        return;
    Chunk *chunk = &m_chunks[(y % m_viewDistance) * m_viewDistance + x % m_viewDistance];

    // One list of vertices per slice of 250 blocks of height, and the indices of its solid and its
    // water faces; kept between calls.
    static std::vector<std::vector<Vertex>> vertices;
    static std::vector<std::vector<int>> solidIndices;
    static std::vector<std::vector<int>> waterIndices;
    vertices.clear();
    solidIndices.clear();
    waterIndices.clear();

    int x0 = x * BlocksPerChunk;
    int y0 = y * BlocksPerChunk;
    m_world.enterZones();
    // A chunk at a zone's border relights 16 columns around it, a chunk that needs a remesh 4.
    if (chunk->needsRemesh || x0 % BlocksPerZone == 0 || y0 % BlocksPerZone == 0 ||
        x0 % BlocksPerZone == BlocksPerZone - BlocksPerChunk || y0 % BlocksPerZone == BlocksPerZone - BlocksPerChunk) {
        int margin = chunk->needsRemesh ? 4 : 16;
        m_world.computeLight(x0 - margin, y0 - margin, x0 + BlocksPerChunk + margin, y0 + BlocksPerChunk + margin, 0,
                             0);
    }

    // The lowest and the highest block that shows a face. Where a column lies below its neighbours,
    // the blocks under it show down to the lowest neighbour's base.
    int minZ = 0;
    int maxZ = 0;
    bool first = true;
    for (int bx = x0; bx < x0 + 32; bx++) {
        for (int by = y0; by < y0 + 32; by++) {
            Field *field = m_world.getField(bx, by);
            if (!field)
                continue;
            int low = field->m_baseZ;
            int top = field->m_size + field->m_baseZ;
            if (top < 1)
                top = 1;
            Field *neighbour = m_world.getField(bx - 1, by);
            if (neighbour)
                low = std::min(neighbour->m_baseZ, low);
            neighbour = m_world.getField(bx + 1, by);
            if (neighbour)
                low = std::min(neighbour->m_baseZ, low);
            neighbour = m_world.getField(bx, by - 1);
            if (neighbour)
                low = std::min(neighbour->m_baseZ, low);
            neighbour = m_world.getField(bx, by + 1);
            if (neighbour)
                low = std::min(neighbour->m_baseZ, low);
            // The top of this neighbour counts as well; the code reads it also where the neighbour is missing.
            maxZ = std::max(neighbour->m_size + neighbour->m_baseZ, maxZ);
            for (int z = low - 1; z < top; z++) {
                unsigned char type = m_world.getBlock(bx, by, z, 0).type & BlockTypeMask;
                if (type == BlockAir)
                    continue;
                if (isFaceOpen(type, m_world.getBlock(bx - 1, by, z, 0).blockType()) ||
                    isFaceOpen(type, m_world.getBlock(bx + 1, by, z, 0).blockType()) ||
                    isFaceOpen(type, m_world.getBlock(bx, by - 1, z, 0).blockType()) ||
                    isFaceOpen(type, m_world.getBlock(bx, by + 1, z, 0).blockType()) ||
                    isFaceOpen(type, m_world.getBlock(bx, by, z - 1, 0).blockType()) ||
                    isFaceOpen(type, m_world.getBlock(bx, by, z + 1, 0).blockType())) {
                    if (first || z < minZ)
                        minZ = z;
                    if (first || z > maxZ)
                        maxZ = z;
                    first = false;
                }
            }
        }
    }

    int slices = (maxZ - minZ) / 250 + 1;
    vertices.resize(slices);
    solidIndices.resize(slices);
    waterIndices.resize(slices);

    // The faces: for every block of every column, a quad towards each neighbour that leaves it open,
    // each corner shaded.
    typedef plasma::Vector<3, int> Normal;
    Zone *zone = m_world.zone(x0 / BlocksPerZone, y0 / BlocksPerZone);
    for (int bx = x0; bx < x0 + 32; bx++) {
        for (int by = y0; by < y0 + 32; by++) {
            Field *field = m_world.getField(bx, by);
            if (!field)
                continue;
            int localX = bx - x0;
            int localY = by - y0;
            int low = field->m_baseZ;
            int top = field->m_size + field->m_baseZ;
            if (top < 1)
                top = 1;
            Field *neighbour = m_world.getField(bx - 1, by);
            if (neighbour)
                low = std::min(neighbour->m_baseZ, low);
            neighbour = m_world.getField(bx + 1, by);
            if (neighbour)
                low = std::min(neighbour->m_baseZ, low);
            neighbour = m_world.getField(bx, by - 1);
            if (neighbour)
                low = std::min(neighbour->m_baseZ, low);
            neighbour = m_world.getField(bx, by + 1);
            if (neighbour)
                low = std::min(neighbour->m_baseZ, low);
            for (int z = low - 1; z < top; z++) {
                int slice = (z - minZ) / 250;
                int sliceBase = slice * 250 + minZ;
                const Block &block = m_world.getBlock(bx, by, z, 0);
                unsigned char type = block.type & BlockTypeMask;
                if (type == BlockAir)
                    continue;
                plasma::Vector<4, float> color =
                    plasma::Vector<4, float>(block.color[0], block.color[1], block.color[2], 255.0f) / 255.0f;
                // Under the stored column the blocks are not kept: the terrain's colour, blended into the colour
                // of the column's lowest block over the first ten blocks when that one is solid.
                if (z < field->m_baseZ) {
                    float depth = (field->m_baseZ - z) * 0.1f;
                    if (depth > 1.0f)
                        depth = 1.0f;
                    plasma::Vector<3, float> terrain;
                    if (field->getBlock(0).isSolid()) {
                        const Block &bottom = field->getBlock(0);
                        terrain = m_world.rockColor(bx, by, z, zone) * depth / 255.0f +
                                  plasma::Vector<3, float>(bottom.color[0], bottom.color[1], bottom.color[2]) *
                                      (1.0f - depth) / 255.0f;
                    } else {
                        terrain = m_world.rockColor(bx, by, z, zone) / 255.0f;
                    }
                    color = plasma::Vector<4, float>(terrain[0], terrain[1], terrain[2], 1.0f);
                }

                // Water is tinted blue by its blue (from 0.1, 0.2, 1 to 0.3, 0.4, 1, RGBA), and its faces go to
                // the water pass.
                std::vector<int> *indices;
                if (type == BlockWater) {
                    color = plasma::Vector<4, float>(0.1f, 0.2f, 1.0f, 1.0f) * (1.0f - color[2]) +
                            plasma::Vector<4, float>(0.3f, 0.4f, 1.0f, 1.0f) * color[2];
                    indices = &waterIndices[slice];
                } else {
                    indices = &solidIndices[slice];
                }

                if (isFaceOpen(type, m_world.getBlock(bx - 1, by, z, 0).blockType())) {
                    addQuad(*indices, vertices[slice].size());
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY, z - sliceBase),
                                                     Normal(-1, 0, 0),
                                                     shade(bx, by, z, color, Normal(-1, 0, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY, z - sliceBase + 1),
                                                     Normal(-1, 0, 0),
                                                     shade(bx, by, z + 1, color, Normal(-1, 0, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY + 1, z - sliceBase + 1),
                                                     Normal(-1, 0, 0),
                                                     shade(bx, by + 1, z + 1, color, Normal(-1, 0, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY + 1, z - sliceBase),
                                                     Normal(-1, 0, 0),
                                                     shade(bx, by + 1, z, color, Normal(-1, 0, 0), false)));
                }
                if (isFaceOpen(type, m_world.getBlock(bx + 1, by, z, 0).blockType())) {
                    addQuad(*indices, vertices[slice].size());
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY, z - sliceBase),
                                                     Normal(1, 0, 0),
                                                     shade(bx + 1, by, z, color, Normal(1, 0, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY + 1, z - sliceBase),
                                                     Normal(1, 0, 0),
                                                     shade(bx + 1, by + 1, z, color, Normal(1, 0, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY + 1, z - sliceBase + 1),
                                                     Normal(1, 0, 0),
                                                     shade(bx + 1, by + 1, z + 1, color, Normal(1, 0, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY, z - sliceBase + 1),
                                                     Normal(1, 0, 0),
                                                     shade(bx + 1, by, z + 1, color, Normal(1, 0, 0), false)));
                }
                if (isFaceOpen(type, m_world.getBlock(bx, by - 1, z, 0).blockType())) {
                    addQuad(*indices, vertices[slice].size());
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY, z - sliceBase),
                                                     Normal(0, -1, 0),
                                                     shade(bx, by, z, color, Normal(0, -1, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY, z - sliceBase),
                                                     Normal(0, -1, 0),
                                                     shade(bx + 1, by, z, color, Normal(0, -1, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY, z - sliceBase + 1),
                                                     Normal(0, -1, 0),
                                                     shade(bx + 1, by, z + 1, color, Normal(0, -1, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY, z - sliceBase + 1),
                                                     Normal(0, -1, 0),
                                                     shade(bx, by, z + 1, color, Normal(0, -1, 0), false)));
                }
                if (isFaceOpen(type, m_world.getBlock(bx, by + 1, z, 0).blockType())) {
                    addQuad(*indices, vertices[slice].size());
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY + 1, z - sliceBase),
                                                     Normal(0, 1, 0),
                                                     shade(bx, by + 1, z, color, Normal(0, 1, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY + 1, z - sliceBase + 1),
                                                     Normal(0, 1, 0),
                                                     shade(bx, by + 1, z + 1, color, Normal(0, 1, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY + 1, z - sliceBase + 1),
                                                     Normal(0, 1, 0),
                                                     shade(bx + 1, by + 1, z + 1, color, Normal(0, 1, 0), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY + 1, z - sliceBase),
                                                     Normal(0, 1, 0),
                                                     shade(bx + 1, by + 1, z, color, Normal(0, 1, 0), false)));
                }
                if (isFaceOpen(type, m_world.getBlock(bx, by, z - 1, 0).blockType())) {
                    addQuad(*indices, vertices[slice].size());
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY, z - sliceBase),
                                                     Normal(0, 0, -1),
                                                     shade(bx, by, z, color, Normal(0, 0, -1), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY + 1, z - sliceBase),
                                                     Normal(0, 0, -1),
                                                     shade(bx, by + 1, z, color, Normal(0, 0, -1), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY + 1, z - sliceBase),
                                                     Normal(0, 0, -1),
                                                     shade(bx + 1, by + 1, z, color, Normal(0, 0, -1), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY, z - sliceBase),
                                                     Normal(0, 0, -1),
                                                     shade(bx + 1, by, z, color, Normal(0, 0, -1), false)));
                }
                if (isFaceOpen(type, m_world.getBlock(bx, by, z + 1, 0).blockType())) {
                    // Wet ground under the open carries a film of water.
                    if (type == BlockWet && (m_world.getBlock(bx, by, z + 1, 0).type & BlockTypeMask) == BlockAir) {
                        addQuad(waterIndices[slice], vertices[slice].size());
                        vertices[slice].push_back(
                            Vertex(plasma::Vector<3, int>(localX, localY, z - sliceBase + 1), Normal(0, 0, 1),
                                   shade(bx, by, z + 1, plasma::Vector<4, float>(0.0f, 0.0f, 1.0f, 1.0f),
                                         Normal(0, 0, 1), false)));
                        vertices[slice].push_back(
                            Vertex(plasma::Vector<3, int>(localX + 1, localY, z - sliceBase + 1), Normal(0, 0, 1),
                                   shade(bx + 1, by, z + 1, plasma::Vector<4, float>(0.0f, 0.0f, 1.0f, 1.0f),
                                         Normal(0, 0, 1), false)));
                        vertices[slice].push_back(
                            Vertex(plasma::Vector<3, int>(localX + 1, localY + 1, z - sliceBase + 1), Normal(0, 0, 1),
                                   shade(bx + 1, by + 1, z + 1, plasma::Vector<4, float>(0.0f, 0.0f, 1.0f, 1.0f),
                                         Normal(0, 0, 1), false)));
                        vertices[slice].push_back(
                            Vertex(plasma::Vector<3, int>(localX, localY + 1, z - sliceBase + 1), Normal(0, 0, 1),
                                   shade(bx, by + 1, z + 1, plasma::Vector<4, float>(0.0f, 0.0f, 1.0f, 1.0f),
                                         Normal(0, 0, 1), false)));
                    }
                    addQuad(*indices, vertices[slice].size());
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY, z - sliceBase + 1),
                                                     Normal(0, 0, 1),
                                                     shade(bx, by, z + 1, color, Normal(0, 0, 1), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY, z - sliceBase + 1),
                                                     Normal(0, 0, 1),
                                                     shade(bx + 1, by, z + 1, color, Normal(0, 0, 1), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX + 1, localY + 1, z - sliceBase + 1),
                                                     Normal(0, 0, 1),
                                                     shade(bx + 1, by + 1, z + 1, color, Normal(0, 0, 1), false)));
                    vertices[slice].push_back(Vertex(plasma::Vector<3, int>(localX, localY + 1, z - sliceBase + 1),
                                                     Normal(0, 0, 1),
                                                     shade(bx, by + 1, z + 1, color, Normal(0, 0, 1), false)));
                }
            }
        }
    }
    m_world.leaveZones();

    plasma::Vector<3, Fixed> boundsMin = plasma::Vector<3, Fixed>(Fixed(x0), Fixed(y0), Fixed(minZ));
    plasma::Vector<3, Fixed> boundsMax(Fixed(x0 + BlocksPerChunk + 1), Fixed(y0 + BlocksPerChunk + 1), Fixed(maxZ + 1));

    // The Direct3D buffers of each slice.
    std::vector<IDirect3DVertexBuffer9 *> vertexBuffers(slices, (IDirect3DVertexBuffer9 *)0);
    std::vector<IDirect3DIndexBuffer9 *> solidBuffers(slices, (IDirect3DIndexBuffer9 *)0);
    std::vector<IDirect3DIndexBuffer9 *> waterBuffers(slices, (IDirect3DIndexBuffer9 *)0);
    for (int i = 0; i < slices; i++) {
        if (!vertices[i].empty()) {
            m_device->CreateVertexBuffer((UINT)(vertices[i].size() * sizeof(Vertex)), D3DUSAGE_WRITEONLY, 0,
                                         D3DPOOL_MANAGED, &vertexBuffers[i], 0);
            void *vertexBufferData;
            vertexBuffers[i]->Lock(0, (UINT)(vertices[i].size() * sizeof(Vertex)), &vertexBufferData, 0);
            memcpy(vertexBufferData, &vertices[i][0], vertices[i].size() * sizeof(Vertex));
            vertexBuffers[i]->Unlock();
        }
        if (!solidIndices[i].empty()) {
            m_device->CreateIndexBuffer((UINT)(solidIndices[i].size() * sizeof(int)), D3DUSAGE_WRITEONLY,
                                        D3DFMT_INDEX32, D3DPOOL_MANAGED, &solidBuffers[i], 0);
            void *solidIndexBufferData;
            solidBuffers[i]->Lock(0, (UINT)(solidIndices[i].size() * sizeof(int)), &solidIndexBufferData, 0);
            memcpy(solidIndexBufferData, &solidIndices[i][0], solidIndices[i].size() * sizeof(int));
            solidBuffers[i]->Unlock();
        }
        if (!waterIndices[i].empty()) {
            m_device->CreateIndexBuffer((UINT)(waterIndices[i].size() * sizeof(int)), D3DUSAGE_WRITEONLY,
                                        D3DFMT_INDEX32, D3DPOOL_MANAGED, &waterBuffers[i], 0);
            void *waterIndexBufferData;
            waterBuffers[i]->Lock(0, (UINT)(waterIndices[i].size() * sizeof(int)), &waterIndexBufferData, 0);
            memcpy(waterIndexBufferData, &waterIndices[i][0], waterIndices[i].size() * sizeof(int));
            waterBuffers[i]->Unlock();
        }
    }

    // The zone's plants in the chunk, lit from their block.
    std::list<Plant> plants;
    m_world.enterZones();
    zone = m_world.zone(x0 / BlocksPerZone, y0 / BlocksPerZone);
    if (zone) {
        for (std::list<Plant>::iterator plant = zone->m_plants.begin(); plant != zone->m_plants.end(); ++plant) {
            if (plasma::Vector<2, int>((int)Fixed((long long)(plant->position[0].raw * 0.03125)),
                                       (int)Fixed((long long)(plant->position[1].raw * 0.03125))) ==
                plasma::Vector<2, int>(x, y)) {
                plant->daylight = (float)m_world.cubeLight((int)plant->position[0], (int)plant->position[1],
                                                           (int)plant->position[2], 0, 0);
                plants.push_back(*plant);
            }
        }
    }
    m_world.leaveZones();

    // The chunk takes the new meshes under its lock; new meshes the chunk thread has not taken yet are
    // deleted.
    EnterCriticalSection(&chunk->lock);
    chunk->center = (boundsMin + boundsMax) * Fixed(0.5);
    chunk->radius = plasma::Vector<3, float>(chunk->center - boundsMax).length() * 1.5f;
    chunk->boundsMin = boundsMin;
    chunk->boundsMax = boundsMax;
    chunk->newPlants = plants;
    for (std::list<ChunkBuffer *>::iterator buffer = chunk->newChunkBuffers.begin();
         buffer != chunk->newChunkBuffers.end(); ++buffer)
        delete *buffer;
    chunk->newChunkBuffers.clear();
    int vertexCount = 0;
    int baseZ = minZ;
    for (int i = 0; i < slices; i++) {
        if (vertexBuffers[i]) {
            ChunkBuffer *buffer = new ChunkBuffer;
            buffer->vertexBuffer = vertexBuffers[i];
            buffer->indexBuffer = solidBuffers[i];
            buffer->waterIndexBuffer = waterBuffers[i];
            buffer->vertexCount = (int)vertices[i].size();
            buffer->primitiveCount = (int)(solidIndices[i].size() / 3);
            buffer->baseZ = baseZ;
            buffer->waterPrimitiveCount = (int)(waterIndices[i].size() / 3);
            chunk->newChunkBuffers.push_back(buffer);
            vertexCount += (int)vertices[i].size();
        }
        baseZ += 250;
    }
    chunk->vertexCount = vertexCount;
    chunk->position = plasma::Vector<2, int>(x, y);
    chunk->needsRemesh = false;
    LeaveCriticalSection(&chunk->lock);
}

// Orders formulas by result level, then rarity, highest first. The key is (level * 6 + rarity) * 1000.
struct FormulaOrder {
    bool operator()(const Formula &a, const Formula &b) const
    {
        return (a.result.level * 6 + a.result.rarity) * 1000 > (b.result.level * 6 + b.result.rarity) * 1000;
    }
};

void GameController::updateFormulas()
{
    // The player's formulas sorted into the crafting window's tabs: weapons, armour, neck and ring
    // items, food, potions and materials.
    std::vector<Formula> weapons;
    std::vector<Formula> armor;
    std::vector<Formula> accessories;
    std::vector<Formula> food;
    std::vector<Formula> potions;
    std::vector<Formula> materials;
    for (std::list<Item>::iterator item = m_player->m_playerData->knownFormulas.begin();
         item != m_player->m_playerData->knownFormulas.end(); ++item) {
        Formula formula;
        if (m_world.findFormula(*item, formula)) {
            switch (item->category) {
            case ItemCategoryWeapon:
                weapons.push_back(formula);
                break;
            case ItemCategoryChest:
            case ItemCategoryHands:
            case ItemCategoryFeet:
            case ItemCategoryShoulder:
                armor.push_back(formula);
                break;
            case ItemCategoryNeck:
            case ItemCategoryRing:
                accessories.push_back(formula);
                break;
            case ItemCategoryConsumable:
                switch (item->id) {
                case ConsumableLifePotion:
                case ConsumableCactusPotion:
                    potions.push_back(formula);
                    break;
                default:
                    food.push_back(formula);
                    break;
                }
                break;
            case ItemCategoryMaterial:
                materials.push_back(formula);
                break;
            }
        }
    }
    std::sort(weapons.begin(), weapons.end(), FormulaOrder());
    std::sort(armor.begin(), armor.end(), FormulaOrder());
    std::sort(accessories.begin(), accessories.end(), FormulaOrder());
    std::sort(food.begin(), food.end(), FormulaOrder());
    std::sort(potions.begin(), potions.end(), FormulaOrder());
    std::sort(materials.begin(), materials.end(), FormulaOrder());
    updateFormulaTab(CraftingTabWeapons, weapons);
    updateFormulaTab(CraftingTabArmor, armor);
    updateFormulaTab(CraftingTabAmulets, accessories);
    updateFormulaTab(CraftingTabCooking, food);
    updateFormulaTab(CraftingTabAlchemy, potions);
    updateFormulaTab(CraftingTabMaterials, materials);
    m_craftingWidget->updateRows();
    m_craftingWidget->updateScrollButton();
}

void GameController::updateFormulaTab(int tab, const std::vector<Formula> &formulas)
{
    if ((int)m_craftingInventory.m_inventoryTabs.size() < tab + 1)
        m_craftingInventory.m_inventoryTabs.resize(tab + 1);
    m_craftingInventory.m_inventoryTabs[tab].clear();
    int index = 0;
    for (std::vector<Formula>::const_iterator formula = formulas.begin(); formula != formulas.end();
         ++formula, ++index) {
        // How often the player's inventory has every ingredient.
        int count = -1;
        for (std::vector<Ingredient>::const_iterator ingredient = formula->ingredients.begin();
             ingredient != formula->ingredients.end(); ++ingredient) {
            int owned = 0;
            for (int i = 0; i < (int)m_player->m_inventory.m_inventoryTabs.size(); i++) {
                for (std::vector<ItemStack>::iterator stack = m_player->m_inventory.m_inventoryTabs[i].begin();
                     stack != m_player->m_inventory.m_inventoryTabs[i].end(); ++stack) {
                    if (stack->item == ingredient->item)
                        owned += stack->quantity;
                }
            }
            int times = owned / ingredient->quantity;
            if (count < 0 || times < count)
                count = times;
        }
        ItemStack stack;
        stack.item = formula->result;
        if (formula->ingredients.empty())
            count = 0;
        stack.quantity = count;
        m_craftingInventory.m_inventoryTabs[tab].push_back(stack);
        // The selected formula shows in the blueprint panel.
        if (tab == m_craftingWidget->m_selectedCell[0] && index == m_craftingWidget->m_selectedCell[1])
            m_blueprintPreview->m_formula = *formula;
    }
}

void GameController::updateStyle()
{
    plasma::Node *tab = m_gameScreen->findNode(L"tab");
    std::list<plasma::Node *> tabs;
    plasma::Node *tabNode;
    // The class's weapon and armour tabs (the rogue's case precedes the mage's:  case blocks
    // are in this order).
    switch (m_player->m_entityData.classType) {
    case ClassWarrior:
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = m_craftMeleeWeaponsIcon->id();
        tabs.push_back(tabNode);
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = m_craftHeavyArmorIcon->id();
        tabs.push_back(tabNode);
        break;
    case ClassRanger:
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = m_craftRangedWeaponsIcon->id();
        tabs.push_back(tabNode);
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = m_craftMediumArmorIcon->id();
        tabs.push_back(tabNode);
        break;
    case ClassRogue:
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = m_craftRogueWeaponsIcon->id();
        tabs.push_back(tabNode);
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = m_craftRogueArmorIcon->id();
        tabs.push_back(tabNode);
        break;
    case ClassMage:
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = m_craftMagicWeaponsIcon->id();
        tabs.push_back(tabNode);
        tabNode = tab->clone(0);
        smoothMeshShape(tabNode->getShape())->m_texture = m_craftLightArmorIcon->id();
        tabs.push_back(tabNode);
        break;
    }
    // Every class's tabs.
    tabNode = tab->clone(0);
    smoothMeshShape(tabNode->getShape())->m_texture = m_craftAmuletsIcon->id();
    tabs.push_back(tabNode);
    tabNode = tab->clone(0);
    smoothMeshShape(tabNode->getShape())->m_texture = m_craftCookingIcon->id();
    tabs.push_back(tabNode);
    tabNode = tab->clone(0);
    smoothMeshShape(tabNode->getShape())->m_texture = m_craftAlchemyIcon->id();
    tabs.push_back(tabNode);
    tabNode = tab->clone(0);
    smoothMeshShape(tabNode->getShape())->m_texture = m_craftIngredientsIcon->id();
    tabs.push_back(tabNode);
    for (std::list<plasma::Node *>::iterator it = tabs.begin(); it != tabs.end(); ++it)
        smoothMeshShape((*it)->getShape())->updateDrawingTextures();
    m_craftingWidget->setTabNodes(tabs);
}

void GameController::updateVendor()
{
    if (m_tradeWidget->m_tabIndex == VendorTabBuyback) {
        m_tradeInventory.m_inventoryTabs.resize(2);
        m_tradeInventory.m_inventoryTabs[VendorTabBuyback].clear();
        for (std::list<ItemStack>::reverse_iterator it = m_soldItems.rbegin(); it != m_soldItems.rend(); ++it)
            m_tradeInventory.m_inventoryTabs[VendorTabBuyback].push_back(*it);
    } else if (m_tradeCreature) {
        m_tradeInventory = m_tradeCreature->m_inventory;
    }
    m_tradeWidget->updateRows();
    m_tradeWidget->updateScrollButton();
}

void GameController::updateWorldList()
// Rows of 600 x 220 pixels, 230 apart from y 50, at x 300 (as refreshCharacterList).
{
    m_selectWorldButton->setParent(0);
    m_worldMenuButton->setParent(0);
    m_deleteWorldButton->setParent(0);
    m_worldListScreen->destroyChildren();
    m_worldRows.clear();
    m_worldRowIndices.clear();
    m_worldRowsByIndex.clear();
    int row = 0;
    for (int i = 0; i < (int)m_worlds.size() + 1; ++i) {
        WorldInfo *world = 0;
        if (i < (int)m_worlds.size()) {
            world = m_worlds[i];
            bool online = world->m_name.size() >= 7 && world->m_name.substr(0, 7) == "online_";
            if (m_showMultiplayerWorlds != online)
                continue;
        }
        plasma::Node *node = m_blackWidget->instance(m_worldListScreen);
        node->setTransformation(node->transformation()->clone());
        node->setDisplay(node->display()->clone());
        node->widget()->resizeTo(600.0f, 220.0f, true);
        plasma::Node *previewNode = m_engine->createNode(0, 0, 0, 0);
        WorldPreviewWidget *preview = new WorldPreviewWidget(m_engine, previewNode, this);
        preview->m_worldInfo = world;
        node->moveIntoWidget(m_blackWidget->getName(), preview, true);
        node->widget()->moveTo(300.0f, (float)(50 + row * 230), true);
        m_worldRows.push_back(node);
        m_worldRowIndices[node] = i;
        m_worldRowsByIndex[i] = node;
        ++row;
    }
    m_worldListScreen->initializeWidgets();
    m_selectWorldButton->setParent(m_worldListScreen);
    m_worldMenuButton->setParent(m_worldListScreen);
    m_deleteWorldButton->setParent(m_worldListScreen);
}

void GameController::useItem(ItemStack *stack)
{
    if (stack->item.category == ItemCategoryConsumable && m_player->m_entityData.stunTime <= 0) {
        if (stack->item.id == ConsumableBomb) {
            Interaction interaction;
            interaction.type = InteractionUseConsumable;
            interaction.item = stack->item;
            m_player->m_interactions.push_back(interaction);
        } else {
            m_player->m_entityData.actionItem = stack->item;
            if (stack->item.id == ConsumableLifePotion || stack->item.id == ConsumableCactusPotion)
                m_player->m_entityData.currentAbility = AbilityDrink;
            else
                m_player->m_entityData.currentAbility = AbilityEat;
            m_player->m_entityData.timeSinceAbility = 0;
            m_player->m_entityData.rollTime = 0;
        }
        --stack->quantity;
        if (stack->quantity <= 0) {
            stack->item.id = 0;
            stack->item.category = ItemCategoryNone;
            stack->quantity = 0;
        }
    }
}

// Writes an item's description at (x, y), line by line: the name (when showName is set) in the
// item's text colour with its power, the level or power line (red when the item is stronger than
// the player), the classes that can use it or "Already known" for a known formula (when
// showClasses is set), a pet's experience and whether it can be ridden, then the statistics
// (damage, HP, armour, resistance, tempo and critical chance as percentages, regeneration) and a
// potion's healing. The last flag is not read.
void GameController::writeItemInfo(
    Item *item, int x, int y, float scale, int width, bool showName, bool showClasses,
    // The tooltip's typography is , in pixels times scale: the text sizes and stroke radii
    // passed to renderText, the line advances, the colours RGBA (0..1). A statistic shows from 0.1
    // (tempo and the percentages from 0.001); a pet's experience is 1000 times its level bonus plus
    // 50.
    bool unusedFlag)
{
    plasma::Vector<2, float> position((float)x, (float)y);
    std::wstringstream text;
    plasma::Vector<4, float> nameColor = item->textColor();
    text.str(L"");
    if (item->category == ItemCategoryPetFood)
        text << "Pet food: ";
    text << m_world.createItemName(*item);
    if (item->scalesWithLevel() && item->category != ItemCategoryPet)
        text << " +" << item->power();
    if (showName) {
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 3.0f, position[0], position[1], scale * 10.0f, 3.0f,
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_WRAP), (float)width, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 3.0f, position[0], position[1], scale * 10.0f, 0.0f,
            nameColor.m_elements, plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_WRAP), (float)width, true);
    }
    text.str(L"");
    position[1] += scale * 30.0f;

    // The level of a pet, the power of other levelled items, the count of the rest.
    plasma::Vector<4, float> color(1.0f, 1.0f, 1.0f, 1.0f);
    if (item->scalesWithLevel()) {
        if (item->category == ItemCategoryPet)
            text << "LVL " << item->level;
        else
            text << "Power " << item->power();
        if (item->power() > m_player->power())
            color = plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f);
    } else if (item->level > 1) {
        text << (int)item->level << " X";
    }
    if (item->flags & (1 << ItemFlagAdapted))
        text << L" (adapted)";
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 2.0f, position[0], position[1], scale * 9.0f, 3.0f, color.m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_WRAP), (float)width, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 2.0f, position[0], position[1], scale * 9.0f, 0.0f, color.m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_WRAP), (float)width, true);

    if (showClasses) {
        text.str(L"");
        position[1] += scale * 14.0f;
        if (!item->fitsClass(m_player->m_entityData.classType)) {
            text.str(L"");
            if (item->fitsClass(ClassWarrior))
                text << L"Warrior ";
            if (item->fitsClass(ClassRanger))
                text << L"Ranger ";
            if (item->fitsClass(ClassMage))
                text << L"Mage ";
            if (item->fitsClass(ClassRogue))
                text << L"Rogue ";
            m_engine->getFontEngine()->renderText(L"resource1.dat", text.str(), 0.0f, 2.0f, position[0], position[1],
                                                  scale * 9.0f, 3.0f,
                                                  plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                                  plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                                  plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                                  (1 << plasma::TEXT_WRAP), (float)width, true);
            m_engine->getFontEngine()->renderText(L"resource1.dat", text.str(), 0.0f, 2.0f, position[0], position[1],
                                                  scale * 9.0f, 0.0f,
                                                  plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                                  plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                                  plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                                  (1 << plasma::TEXT_WRAP), (float)width, true);
        } else if (item->category == ItemCategoryFormula) {
            // A formula's result: the item with the formula's category.
            Item result = *item;
            result.category = result.formulaCategory;
            result.formulaCategory = ItemCategoryNone;
            if (m_player->knowsFormula(result)) {
                position[1] += scale * 27.0f;
                std::wstring known(L"Already known");
                m_engine->getFontEngine()->renderText(L"resource1.dat", known, 0.0f, 2.0f, position[0], position[1],
                                                      scale * 9.0f, 3.0f,
                                                      plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                                      plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                                      plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                                      (1 << plasma::TEXT_WRAP), (float)width, true);
                m_engine->getFontEngine()->renderText(L"resource1.dat", known, 0.0f, 2.0f, position[0], position[1],
                                                      scale * 9.0f, 0.0f,
                                                      plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                                      plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                                      plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                                      (1 << plasma::TEXT_WRAP), (float)width, true);
            }
        }
    }
    text.str(L"");
    position[1] += scale * 16.0f;

    // A pet's experience towards its next level, and whether it can be ridden.
    if (item->category == ItemCategoryPet) {
        int experience = (int)(levelBonus(item->level) * 1000.0f + 50.0f);
        text.str(L"");
        text << L"XP " << item->modifier << "/" << experience;
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
            plasma::Vector<4, float>(0.5f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        position[1] += scale * 14.0f;
        if (isPet(item->id)) {
            text.str(L"");
            text << L"Ridable";
            m_engine->getFontEngine()->renderText(
                L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
                plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
            m_engine->getFontEngine()->renderText(
                L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
                plasma::Vector<4, float>(0.75f, 0.5f, 1.0f, 1.0f).m_elements,
                plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
            position[1] += scale * 14.0f;
        }
    }
    // A weapon's damage.
    if (item->category == ItemCategoryWeapon) {
        text.str(L"");
        text << "DMG " << roundTenth(item->weaponDamage());
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
            plasma::Vector<4, float>(0.75f, 0.5f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        position[1] += scale * 14.0f;
    }
    // Health.
    if (item->hp() >= 0.1f) {
        text.str(L"");
        text << "HP " << roundTenth(item->hp());
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
            plasma::Vector<4, float>(0.75f, 0.5f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        position[1] += scale * 14.0f;
    }
    // Armour.
    if (item->armor() >= 0.1f) {
        text.str(L"");
        text << "ARMOR " << roundTenth(item->armor());
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
            plasma::Vector<4, float>(0.75f, 0.5f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        position[1] += scale * 16.0f;
    }
    // Resistance.
    if (item->resistance() >= 0.1f) {
        text.str(L"");
        text << "RESI " << roundTenth(item->resistance());
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
            plasma::Vector<4, float>(0.75f, 0.5f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        position[1] += scale * 16.0f;
    }
    // Tempo, as a percentage.
    if (item->tempo() >= 0.001f) {
        text.str(L"");
        text << "TEMPO " << roundTenth(item->tempo() * 100.0f) << "%";
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
            plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        position[1] += scale * 16.0f;
    }
    // Critical chance, as a percentage.
    if (item->critical() >= 0.001f) {
        text.str(L"");
        text << "CRIT " << roundTenth(item->critical() * 100.0f) << "%";
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
            plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        position[1] += scale * 16.0f;
    }
    // Regeneration.
    if (item->regeneration() >= 0.1f) {
        text.str(L"");
        text << "REG " << roundTenth(item->regeneration());
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
            plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
            plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        position[1] += scale * 16.0f;
    }
    // A potion's healing.
    if (item->category == ItemCategoryConsumable && item->effectValue() >= 0.1f) {
        text.str(L"");
        text << "HP +" << (int)item->effectValue();
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 2.0f,
            plasma::Vector<4, float>(0.25f, 1.0f, 0.25f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        m_engine->getFontEngine()->renderText(
            L"resource1.dat", text.str(), 0.0f, 0.0f, position[0], position[1], scale * 10.0f, 0.0f,
            plasma::Vector<4, float>(0.25f, 1.0f, 0.25f, 1.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
            plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, plasma::TEXT_FLAGS_NONE, -1.0f, true);
        position[1] += scale * 14.0f;
    }
}

// Lays the screens out again for the window size (m_windowWidth and m_windowHeight hold the new
// size already): nodes placed by their translation (less their pivot where the code subtracts it)
// and widgets moved, mostly centred or kept at an edge 20 pixels in; the inventory's neighbours
// are placed beside it. Then the pointer is moved to the window's centre.
void GameController::resize(int width, int height)
// The offsets are pixels.
{
    if (m_dialogNode) {
        m_dialogNode->transformation()->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2) - m_dialogWidget->viewWidth() * 0.5f, 300.0f);
        m_dialogNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    if (m_questTag) {
        m_questTag->transformation()->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight - 300)) -
            m_questTag->transformation()->m_pivot.value();
        m_questTag->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    if (m_smallQuestTag) {
        m_smallQuestTag->transformation()->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth - 200), 400.0f) -
            m_smallQuestTag->transformation()->m_pivot.value();
        m_smallQuestTag->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    if (m_cubeWorldLogo) {
        m_cubeWorldLogo->transformation()->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight / 2)) -
            m_cubeWorldLogo->transformation()->m_pivot.value();
        m_cubeWorldLogo->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    plasma::Widget *widget = m_startMenuNode->widget();
    widget->moveTo(((float)m_windowWidth - widget->viewWidth()) * 0.5f, (float)(m_windowHeight / 2 + 50), true);
    if (m_picromaLogo) {
        m_picromaLogo->transformation()->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth - 20), (float)(m_windowHeight - 20)) -
            m_picromaLogo->transformation()->m_pivot.value();
        m_picromaLogo->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    m_comboPoints->transformation()->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight - 190)) -
        m_comboPoints->transformation()->m_pivot.value();
    m_comboPoints->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);

    m_waitNode->transformation()->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight / 2));
    m_waitNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_nameError->transformation()->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight - 200));
    m_nameError->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_connectionErrorNode->transformation()->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight / 2 + 100));
    m_connectionErrorNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);

    // The character screens.
    m_characterNameNode->transformation()->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight - 50));
    m_characterNameNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    widget = m_createCharacterButton->widget();
    widget->moveTo((float)(m_windowWidth - 20) - widget->viewWidth(),
                   (float)(m_windowHeight - 20) - widget->getViewHeight(), true);
    widget = m_characterStylePanel->widget();
    widget->moveTo(20.0f, (float)(m_windowHeight - 20) - widget->getViewHeight(), true);
    widget = m_optionsPanel->widget();
    widget->moveTo(((float)m_windowWidth - widget->viewWidth()) * 0.5f,
                   ((float)m_windowHeight - widget->getViewHeight()) * 0.5f, true);
    widget = m_systemPanel->widget();
    widget->moveTo(((float)m_windowWidth - widget->viewWidth()) * 0.5f,
                   ((float)m_windowHeight - widget->getViewHeight()) * 0.5f, true);

    // The world screens.
    m_backButton->widget()->moveTo(20.0f, 20.0f, true);
    widget = m_worldListButton->widget();
    widget->moveTo((float)m_windowWidth - widget->viewWidth() - 20.0f, 20.0f, true);
    m_serverAddressNode->transformation()->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight / 2 - 100));
    m_serverAddressNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    widget = m_connectScreenButton->widget();
    widget->moveTo((float)m_windowWidth - widget->viewWidth() - 20.0f,
                   (float)m_windowHeight - widget->getViewHeight() - 20.0f, true);
    widget = m_connectButton->widget();
    widget->moveTo(((float)m_windowWidth - widget->viewWidth()) * 0.5f,
                   ((float)m_windowHeight - widget->getViewHeight()) * 0.5f, true);
    m_worldNameNode->transformation()->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight - 50));
    m_worldNameNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_worldSeedNode->transformation()->m_translation =
        plasma::Vector<2, float>((float)(m_windowWidth / 2), (float)(m_windowHeight - 100));
    m_worldSeedNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    widget = m_createWorldButton->widget();
    widget->moveTo((float)(m_windowWidth - 20) - widget->viewWidth(),
                   (float)(m_windowHeight - 20) - widget->getViewHeight(), true);
    for (std::vector<plasma::Node *>::iterator it = m_characterRows.begin(); it != m_characterRows.end(); ++it) {
        widget = (*it)->widget();
        widget->moveTo((float)(m_windowWidth / 2) - widget->viewWidth() * 0.5f, widget->viewY(), true);
    }

    // The game screen: the ability buttons 48 pixels apart and the menu buttons 60 apart, centred.
    for (int i = 0; i < (int)m_abilityButtons.size(); i++) {
        plasma::Transformation *transformation = m_abilityButtons[i]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2 + (i * 2 - m_abilityButtons.size()) * 24 + 6),
                                     (float)(m_windowHeight - 50)) -
            transformation->m_pivot.value();
        transformation->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    for (int i = 0; i < (int)m_menuButtons.size(); i++) {
        plasma::Transformation *transformation = m_menuButtons[i]->transformation();
        transformation->m_translation =
            plasma::Vector<2, float>((float)(m_windowWidth / 2 + (i * 2 - m_menuButtons.size() + 1) * 30),
                                     (float)(m_windowHeight - 140)) -
            transformation->m_pivot.value();
        transformation->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    if (m_inventoryNode) {
        widget = m_inventoryNode->widget();
        widget->moveTo((float)(m_windowWidth - 20) - widget->viewWidth(),
                       (float)m_windowHeight - widget->getViewHeight() - 220.0f, false);
    }
    if (m_landNameNode) {
        m_landNameNode->transformation()->m_translation = plasma::Vector<2, float>((float)(m_windowWidth / 2), 230.0f) -
                                                          m_landNameNode->transformation()->m_pivot.value();
        m_landNameNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    m_chatWidget->moveTo(20.0f, (float)(m_windowHeight - 220), true);
    if (m_voxelPanel) {
        m_voxelPanel->transformation()->m_translation =
            plasma::Vector<2, float>((float)(m_engine->viewportWidth() / 2 - 340),
                                     (float)(m_engine->viewportHeight() / 2 - 250)) -
            m_voxelPanel->transformation()->m_pivot.value();
        m_voxelPanel->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    if (m_objectiveNode) {
        m_objectiveNode->transformation()->m_translation = plasma::Vector<2, float>(20.0f, 170.0f);
        m_objectiveNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    if (m_statisticsNode) {
        m_statisticsNode->transformation()->m_translation = plasma::Vector<2, float>(20.0f, 470.0f);
        m_statisticsNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    // The equipment slots in two columns 400 pixels apart, 56 pixels a row.
    for (int i = 0; i < (int)m_equipmentBoxes.size(); i++) {
        plasma::Transformation *transformation = m_equipmentBoxes[i]->transformation();
        transformation->m_translation = plasma::Vector<2, float>((float)(m_windowWidth / 2 - 200 + i % 2 * 400),
                                                                 (float)(m_windowHeight / 2 + i / 2 * 56 + 10)) -
                                        transformation->m_pivot.value();
        m_equipmentBoxes[i]->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    }
    m_characterWidget->getParentWidget()->moveTo(20.0f, 340.0f, true);

    // The inventory centred, the crafting, blueprint and other panels beside it, 10 pixels apart.
    if (m_inventoryNode) {
        plasma::Widget *inventory = m_inventoryNode->widget();
        m_inventoryNode->widget()->setViewPosition(
            plasma::Vector<2, float>((float)(m_windowWidth / 2) - inventory->viewWidth() * 0.5f,
                                     (float)(m_windowHeight / 2) - inventory->getViewHeight()),
            true);
    }
    if (m_craftingNode) {
        plasma::Widget *inventory = m_inventoryNode->widget();
        plasma::Widget *crafting = m_craftingNode->widget();
        m_craftingNode->widget()->setViewPosition(
            plasma::Vector<2, float>((float)(m_windowWidth / 2) - inventory->viewWidth() * 0.5f -
                                         crafting->viewWidth() - 10.0f,
                                     (float)(m_windowHeight / 2) - inventory->getViewHeight()),
            true);
    }
    if (m_blueprintNode) {
        plasma::Widget *inventory = m_inventoryNode->widget();
        plasma::Widget *crafting = m_craftingNode->widget();
        plasma::Widget *blueprint = m_blueprintNode->widget();
        m_blueprintNode->widget()->setViewPosition(
            plasma::Vector<2, float>(
                (float)(m_windowWidth / 2) - inventory->viewWidth() * 0.5f - blueprint->viewWidth() - 10.0f,
                (float)(m_windowHeight / 2) - inventory->getViewHeight() + 10.0f + crafting->getViewHeight()),
            true);
    }
    if (m_skillPanel) {
        plasma::Widget *inventory = m_inventoryNode->widget();
        plasma::Widget *panel = m_skillPanel->widget();
        m_skillPanel->widget()->setViewPosition(
            plasma::Vector<2, float>((float)(m_windowWidth / 2) - inventory->viewWidth() * 0.5f - panel->viewWidth() -
                                         10.0f,
                                     (float)(m_windowHeight / 2) - inventory->getViewHeight()),
            true);
    }
    if (m_tradePanel) {
        plasma::Widget *inventory = m_inventoryNode->widget();
        m_tradePanel->widget()->setViewPosition(
            plasma::Vector<2, float>((float)(m_windowWidth / 2) + inventory->viewWidth() * 0.5f + 10.0f,
                                     (float)(m_windowHeight / 2) - inventory->getViewHeight()),
            true);
    }
    if (m_enchantPanel) {
        plasma::Widget *inventory = m_inventoryNode->widget();
        m_enchantPanel->widget()->setViewPosition(
            plasma::Vector<2, float>((float)(m_windowWidth / 2) + inventory->viewWidth() * 0.5f + 10.0f,
                                     (float)(m_windowHeight / 2) - inventory->getViewHeight()),
            true);
    }
    if (m_adaptionPanel) {
        plasma::Widget *inventory = m_inventoryNode->widget();
        m_adaptionPanel->widget()->setViewPosition(
            plasma::Vector<2, float>((float)(m_windowWidth / 2) + inventory->viewWidth() * 0.5f + 10.0f,
                                     (float)(m_windowHeight / 2) - inventory->getViewHeight()),
            true);
    }
    if (m_voxelPanel) {
        plasma::Widget *inventory = m_inventoryNode->widget();
        plasma::Widget *panel = m_voxelPanel->widget();
        m_voxelPanel->widget()->setViewPosition(
            plasma::Vector<2, float>((float)(m_windowWidth / 2) - inventory->viewWidth() * 0.5f - panel->viewWidth() -
                                         10.0f,
                                     (float)(m_windowHeight / 2) - inventory->getViewHeight()),
            true);
    }
    m_helpNode->transformation()->m_translation = plasma::Vector<2, float>((float)(m_windowWidth / 2 - 430), 50.0f);
    m_helpNode->transformation()->attributesChanged(plasma::ATTRIBUTES_CHANGED_ALL);
    m_engine->movePointer((float)(width / 2), (float)(height / 2));
}

// The skill's text: skills 6 to 8 describe the class's ability (by the specialization chosen on
// the skill screen), the others the skill itself; then its effects at the current level (if any)
// and at the next level.
void GameController::writeSkillInfo(int skill, int level, int x, int y, float scale, int width)
// The text's typography, in pixels: the first dialog 12 high, the others 10; a wrapped line
// advances 4 more than the size, a dialog 18; words are spaced by the size, punctuation by 3, and
// a slash or an opening bracket pulls the next word 9 closer; the stroke radius is 3.
{
    plasma::ScalableFont *font = m_engine->getFontEngine()->getFont(L"resource1.dat");
    if (!font)
        return;
    std::vector<Dialog> dialogs;
    Speech::Context context;
    std::wstring key;
    switch (skill) {
    case SkillClassAbilityFirst:
        switch (m_player->m_entityData.classType) {
        case ClassWarrior:
            key = m_world.abilityKey(AbilitySmash);
            break;
        case ClassRanger:
            key = m_world.abilityKey(AbilityRangerKick);
            break;
        case ClassMage:
            if (m_skillWidget->m_specialization == SpecializationWater)
                key = m_world.abilityKey(AbilityHealingStream);
            else
                key = m_world.abilityKey(AbilityFireExplosion);
            break;
        case ClassRogue:
            key = m_world.abilityKey(AbilityIntercept);
            break;
        }
        break;
    case SkillClassAbilitySecond:
        switch (m_player->m_entityData.classType) {
        case ClassWarrior:
            key = m_world.abilityKey(AbilityCyclone);
            break;
        case ClassRanger:
            key = m_world.abilityKey(AbilityRetreat);
            break;
        case ClassMage:
            key = m_world.abilityKey(AbilityManaShield);
            break;
        case ClassRogue:
            key = m_world.abilityKey(AbilitySneak);
            break;
        }
        break;
    case SkillClassAbilityThird:
        switch (m_player->m_entityData.classType) {
        case ClassWarrior:
            if (m_skillWidget->m_specialization == SpecializationBerserker)
                key = m_world.abilityKey(AbilityWarFrenzy);
            else
                key = m_world.abilityKey(AbilityBulwark);
            break;
        case ClassRanger:
            if (m_skillWidget->m_specialization == SpecializationScout)
                key = m_world.abilityKey(AbilitySwiftness);
            else
                key = m_world.abilityKey(AbilityAim);
            break;
        case ClassMage:
            key = m_world.abilityKey(AbilityTeleport);
            break;
        case ClassRogue:
            if (m_skillWidget->m_specialization == SpecializationNinja)
                key = m_world.abilityKey(AbilityShuriken);
            else
                key = m_world.abilityKey(AbilityCamouflage);
            break;
        }
        break;
    default:
        key = m_world.skillKey(skill);
        break;
    }
    getSkillDetails(skill, level, &context);
    m_world.m_speech.generateDialogs(std::wstring(L"description:") + key, &context, dialogs);
    if (level > 0) {
        m_world.m_speech.generateDialogs(L"skill:level", &context, dialogs);
        m_world.m_speech.generateDialogs(std::wstring(L"details:") + key, &context, dialogs);
    }
    getSkillDetails(skill, level + 1, &context);
    m_world.m_speech.generateDialogs(L"skill:nextlevel", &context, dialogs);
    m_world.m_speech.generateDialogs(std::wstring(L"details:") + key, &context, dialogs);

    int size = 12;
    for (std::vector<Dialog>::iterator dialog = dialogs.begin(); dialog != dialogs.end(); ++dialog) {
        int lineX = x;
        bool noSpace = true;
        for (std::list<Dialog::Phrase>::iterator phrase = dialog->phrases.begin(); phrase != dialog->phrases.end();
             ++phrase) {
            plasma::Matrix<float> matrix;
            m_engine->getModelViewMatrix(matrix);
            plasma::Vector<2, float> minimum;
            plasma::Vector<2, float> maximum;
            font->measure(phrase->text, matrix, (float)size, 0.0f, 0.0f, 0.0f, 0.0f, minimum, maximum, 0, true, -1,
                          false);
            int wordWidth = (int)(maximum[0] - minimum[0]);
            // Punctuation follows the previous word closely and never wraps.
            bool punctuation = phrase->text == L"." || phrase->text == L"-" || phrase->text == L"," ||
                               phrase->text == L";" || phrase->text == L"!" || phrase->text == L"?" ||
                               phrase->text == L":" || phrase->text == L"/" || phrase->text == L")";
            if (!noSpace) {
                if (punctuation)
                    lineX += 3;
                else
                    lineX += size;
            }
            noSpace = phrase->text == L"-";
            if (!punctuation && lineX + wordWidth > x + width) {
                lineX = x;
                y += 4 + size;
            }
            font->renderText(phrase->text, (float)lineX, (float)y, (float)size, 3.0f, 0.0f, 0.0f, 0.0f,
                             plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                             plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                             plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, 0, true);
            font->renderText(phrase->text, (float)lineX, (float)y, (float)size, 0.0f, 0.0f, 0.0f, 0.0f,
                             phrase->color.m_elements, plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                             plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, 0, true);
            lineX += wordWidth;
            // A slash or an opening bracket pulls the next word closer.
            if (phrase->text == L"/" || phrase->text == L"(")
                lineX -= 9;
        }
        y += 18;
        size = 10;
    }
}

void GameController::writeSpecializationInfo(int classType, int specialization, int x, int y,
                                             // The typography is writeSkillInfo's.
                                             float scale, int width)
{
    plasma::ScalableFont *font = m_engine->getFontEngine()->getFont(L"resource1.dat");
    if (!font)
        return;
    std::vector<Dialog> dialogs;
    Speech::Context context;
    switch (classType) {
    case ClassWarrior:
        switch (specialization) {
        case SpecializationBerserker:
            m_world.m_speech.generateDialogs(L"specialization:warrior:berserker", &context, dialogs);
            break;
        case SpecializationGuardian:
            m_world.m_speech.generateDialogs(L"specialization:warrior:guardian", &context, dialogs);
            break;
        }
        break;
    case ClassRanger:
        switch (specialization) {
        case SpecializationScout:
            m_world.m_speech.generateDialogs(L"specialization:ranger:scout", &context, dialogs);
            break;
        case SpecializationSniper:
            m_world.m_speech.generateDialogs(L"specialization:ranger:sniper", &context, dialogs);
            break;
        }
        break;
    case ClassMage:
        switch (specialization) {
        case SpecializationFire:
            m_world.m_speech.generateDialogs(L"specialization:mage:fire", &context, dialogs);
            break;
        case SpecializationWater:
            m_world.m_speech.generateDialogs(L"specialization:mage:water", &context, dialogs);
            break;
        }
        break;
    case ClassRogue:
        switch (specialization) {
        case SpecializationAssassin:
            m_world.m_speech.generateDialogs(L"specialization:rogue:assassin", &context, dialogs);
            break;
        case SpecializationNinja:
            m_world.m_speech.generateDialogs(L"specialization:rogue:ninja", &context, dialogs);
            break;
        }
        break;
    }

    int size = 12;
    for (std::vector<Dialog>::iterator dialog = dialogs.begin(); dialog != dialogs.end(); ++dialog) {
        int lineX = x;
        bool noSpace = true;
        for (std::list<Dialog::Phrase>::iterator phrase = dialog->phrases.begin(); phrase != dialog->phrases.end();
             ++phrase) {
            plasma::Matrix<float> matrix;
            m_engine->getModelViewMatrix(matrix);
            plasma::Vector<2, float> minimum;
            plasma::Vector<2, float> maximum;
            font->measure(phrase->text, matrix, (float)size, 0.0f, 0.0f, 0.0f, 0.0f, minimum, maximum, 0, true, -1,
                          false);
            int wordWidth = (int)(maximum[0] - minimum[0]);
            // Punctuation follows the previous word closely and never wraps.
            bool punctuation = phrase->text == L"." || phrase->text == L"-" || phrase->text == L"," ||
                               phrase->text == L";" || phrase->text == L"!" || phrase->text == L"?" ||
                               phrase->text == L":" || phrase->text == L"/" || phrase->text == L")";
            if (!noSpace) {
                if (punctuation)
                    lineX += 3;
                else
                    lineX += size;
            }
            noSpace = phrase->text == L"-";
            if (!punctuation && lineX + wordWidth > x + width) {
                lineX = x;
                y += 4 + size;
            }
            font->renderText(phrase->text, (float)lineX, (float)y, (float)size, 3.0f, 0.0f, 0.0f, 0.0f,
                             plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                             plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                             plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, 0, true);
            font->renderText(phrase->text, (float)lineX, (float)y, (float)size, 0.0f, 0.0f, 0.0f, 0.0f,
                             phrase->color.m_elements, plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                             plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, 0, true);
            lineX += wordWidth;
            // A slash or an opening bracket pulls the next word closer.
            if (phrase->text == L"/" || phrase->text == L"(")
                lineX -= 9;
        }
        y += 18;
        size = 10;
    }
}

}
