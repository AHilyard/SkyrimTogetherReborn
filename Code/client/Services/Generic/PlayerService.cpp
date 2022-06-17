#include <Services/PlayerService.h>

#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/GridCellChangeEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/PlayerDialogueEvent.h>
#include <Events/PlayerMapMarkerUpdateEvent.h>
#include <Events/PlayerLevelEvent.h>
#include <Events/PlayerSetWaypointEvent.h>
#include <Events/PlayerDelWaypointEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/PlayerMapMarkerUpdateEvent.h>
#include <Events/MapOpenEvent.h>
#include <Events/MapCloseEvent.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/ShiftGridCellRequest.h>
#include <Messages/EnterExteriorCellRequest.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/RequestDelWaypoint.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/PlayerDialogueRequest.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/NotifyPlayerPosition.h>
#include <Messages/NotifyPlayerCellChanged.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/NotifyDelWaypoint.h>


#include <Structs/ServerSettings.h>

#include <Interface/Menus/MapMenu.h>
#include <Interface/UI.h>
#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESGlobal.h>
#include <Games/Overrides.h>
#include <Games/References.h>
#include <AI/AIProcess.h>
#include <Forms/TESWorldSpace.h>
#include <ExtraData/ExtraMapMarker.h>

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept 
    : m_world(aWorld), m_dispatcher(aDispatcher), m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&PlayerService::OnUpdate>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&PlayerService::OnConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&PlayerService::OnDisconnected>(this);
    m_settingsConnection = m_dispatcher.sink<ServerSettings>().connect<&PlayerService::OnServerSettingsReceived>(this);
    m_playerJoinedConnection = m_dispatcher.sink<NotifyPlayerJoined>().connect<&PlayerService::OnPlayerJoined>(this);
    m_playerLeftConnection = m_dispatcher.sink<NotifyPlayerLeft>().connect<&PlayerService::OnPlayerLeft>(this);
    m_playerNotifySetWaypointConnection = m_dispatcher.sink<NotifySetWaypoint>().connect<&PlayerService::OnNotifyPlayerSetWaypoint>(this);
    m_playerNotifyDelWaypointConnection = m_dispatcher.sink<NotifyDelWaypoint>().connect<&PlayerService::OnNotifyPlayerDelWaypoint>(this);
    m_notifyRespawnConnection = m_dispatcher.sink<NotifyPlayerRespawn>().connect<&PlayerService::OnNotifyPlayerRespawn>(this);
    m_gridCellChangeConnection = m_dispatcher.sink<GridCellChangeEvent>().connect<&PlayerService::OnGridCellChangeEvent>(this);
    m_cellChangeConnection = m_dispatcher.sink<CellChangeEvent>().connect<&PlayerService::OnCellChangeEvent>(this);
    m_playerDialogueConnection = m_dispatcher.sink<PlayerDialogueEvent>().connect<&PlayerService::OnPlayerDialogueEvent>(this);
    m_playerMapMarkerConnection = m_dispatcher.sink<PlayerMapMarkerUpdateEvent>().connect<&PlayerService::OnPlayerMapMarkerUpdateEvent>(this);
    m_playerLevelConnection = m_dispatcher.sink<PlayerLevelEvent>().connect<&PlayerService::OnPlayerLevelEvent>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&PlayerService::OnPartyJoinedEvent>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&PlayerService::OnPartyLeftEvent>(this);
    m_playerPosition = m_dispatcher.sink<NotifyPlayerPosition>().connect<&PlayerService::OnNotifyPlayerPosition>(this);
    m_playerPositionConnection = m_dispatcher.sink<NotifyPlayerPosition>().connect<&PlayerService::OnNotifyPlayerPosition>(this);
    m_playerCellChangeConnection = m_dispatcher.sink<NotifyPlayerCellChanged>().connect<&PlayerService::OnNotifyPlayerCellChanged>(this);
    m_playerSetWaypointConnection = m_dispatcher.sink<PlayerSetWaypointEvent>().connect<&PlayerService::OnPlayerSetWaypoint>(this);
    m_playerDelWaypointConnection =m_dispatcher.sink<PlayerDelWaypointEvent>().connect<&PlayerService::OnPlayerDelWaypoint>(this);
    m_mapOpenConnection = m_dispatcher.sink<MapOpenEvent>().connect<&PlayerService::OnMapOpen>(this);
    m_mapCloseConnection = m_dispatcher.sink<MapCloseEvent>().connect<&PlayerService::OnMapClose>(this);
}


// TODO: this whole thing should probably be a util function by now
TESObjectCELL* PlayerService::GetCell(const GameId& acCellId, const GameId& acWorldSpaceId, const GridCellCoords& acCenterCoords) const noexcept
{
    auto& modSystem = m_world.GetModSystem();
    uint32_t cellId = modSystem.GetGameId(acCellId);
    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(cellId));

    if (!pCell)
    {
        const uint32_t cWorldSpaceId = m_world.GetModSystem().GetGameId(acWorldSpaceId);
        TESWorldSpace* const pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(cWorldSpaceId));
        if (pWorldSpace)
            pCell = pWorldSpace->LoadCell(acCenterCoords.X, acCenterCoords.Y);
    }

    return pCell;
}

bool DeleteMarkerDummy(const uint32_t acHandle) noexcept
{
    auto* pDummyPlayer = TESObjectREFR::GetByHandle(acHandle);
    if (!pDummyPlayer)
        return false;

    pDummyPlayer->Delete();

    PlayerCharacter::Get()->RemoveMapmarkerRef(acHandle);

    return true;
}

static bool knockdownStart = false;
static double knockdownTimer = 0.0;

static bool godmodeStart = false;
static double godmodeTimer = 0.0;

void PlayerService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunRespawnUpdates(acEvent.Delta);
    RunPostDeathUpdates(acEvent.Delta);
    RunDifficultyUpdates();
    RunLevelUpdates();
    RunMapUpdates();
}

void PlayerService::CreateDummyMarker()
{
    TESObjectREFR* dummyMark = TESObjectREFR::New();
    dummyMark->SetBaseForm(TESForm::GetById(0x10));
    dummyMark->SetSkipSaveFlag(true);
    dummyMark->position.x = -INTMAX_MAX;
    dummyMark->position.y = -INTMAX_MAX;

    MapMarkerData* dummyData = MapMarkerData::New();
    dummyData->name.value.Set("Custom Destination");
    dummyData->cOriginalFlags = dummyData->cFlags = MapMarkerData::Flag::VISIBLE;
    dummyData->sType = MapMarkerData::Type::kMultipleQuest; // "custom destination" marker either 66 or 0
    dummyMark->extraData.SetMarkerData(dummyData);

    uint32_t handle;
    dummyMark->GetHandle(handle);
    PlayerCharacter::Get()->AddMapmarkerRef(handle);

    MapHandleInfo dummy = {-1, handle};

    m_mapHandles.push_back(dummy);
}

PlayerService::MapHandleInfo* PlayerService::GetDummyMarker()
{
    for (auto it = m_mapHandles.begin(); it != m_mapHandles.end(); it++)
    {
        if (it->PlayerId = -1)
        {
            return &*it;
        }
    }
    return nullptr;
}

void PlayerService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    //Create Dummy Map Markers
    for (int i = 0; i < m_initDummyMarkers; i++)
    {
        CreateDummyMarker();
    }

    // TODO: a MapService is warranted at this point.
    m_waypoint = TESObjectREFR::New();
    m_waypoint->SetBaseForm(TESForm::GetById(0x10));
    m_waypoint->SetSkipSaveFlag(true);
    m_waypoint->position.x = -INTMAX_MAX;
    m_waypoint->position.y = -INTMAX_MAX;

    m_waypointData = MapMarkerData::New();
    m_waypointData->name.value.Set("Custom Destination");
    m_waypointData->cOriginalFlags = m_waypointData->cFlags = MapMarkerData::Flag::VISIBLE;
    m_waypointData->sType = MapMarkerData::Type::kCustomMarker; // "custom destination" marker either 66 or 0
    m_waypoint->extraData.SetMarkerData(m_waypointData);

    uint32_t handle;
    m_waypoint->GetHandle(handle);
    PlayerCharacter::Get()->AddMapmarkerRef(handle);

    // TODO: SkyrimTogether.esm
    TESGlobal* pKillMove = Cast<TESGlobal>(TESForm::GetById(0x100F19));
    pKillMove->f = 0.f;

#if TP_SKYRIM64
    TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
    pWorldEncountersEnabled->f = 0.f;
#elif TP_FALLOUT4
    // Makes it so that VATS doesn't slow down time
    float* vatsTargetingMult = Settings::GetVATSSelectTargetTimeMultiplier();
    *vatsTargetingMult = 0.f;

    // TODO(ft): disable world encounters
#endif
}

void PlayerService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    auto* pPlayer = PlayerCharacter::Get();

    pPlayer->SetDifficulty(m_previousDifficulty);
    m_serverDifficulty = m_previousDifficulty = 6;

    ToggleDeathSystem(false);

    TESGlobal* pKillMove = Cast<TESGlobal>(TESForm::GetById(0x100F19));
    pKillMove->f = 1.f;

    // Restore to the default value (150 in skyrim, 175 in fallout 4)
    float* greetDistance = Settings::GetGreetDistance();
#if TP_SKYRIM64
    *greetDistance = 150.f;

    TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
    pWorldEncountersEnabled->f = 1.f;
#elif TP_FALLOUT4
    *greetDistance = 175.f;

    // Restore VATS slow time (default is 0.04)
    float* vatsTargetingMult = Settings::GetVATSSelectTargetTimeMultiplier();
    *vatsTargetingMult = 0.04f;
#endif

    for (auto it = m_mapHandles.begin(); it != m_mapHandles.end(); it++)
    {
        DeleteMarkerDummy(it->handle);
    }
    m_mapHandles.clear();

    if (m_waypoint)
        m_waypoint->Delete();
    m_waypoint = nullptr;
}

void PlayerService::OnServerSettingsReceived(const ServerSettings& acSettings) noexcept
{
    m_previousDifficulty = *Settings::GetDifficulty();
    PlayerCharacter::Get()->SetDifficulty(acSettings.Difficulty);
    m_serverDifficulty = acSettings.Difficulty;

    if (!acSettings.GreetingsEnabled)
    {
        float* greetDistance = Settings::GetGreetDistance();
        *greetDistance = 0.f;
    }

    ToggleDeathSystem(acSettings.DeathSystemEnabled);
}

void PlayerService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    //Associate a dummy marker with this player id
    MapHandleInfo *dummyInfo = GetDummyMarker();
    dummyInfo->PlayerId = acMessage.PlayerId;

    TESObjectREFR* pPlayer = TESObjectREFR::GetByHandle(dummyInfo->handle);

    TESObjectCELL* pCell = GetCell(acMessage.CellId, acMessage.WorldSpaceId, acMessage.CenterCoords);

    TP_ASSERT(pCell, "Cell not found for joined player");

    if (pCell)
        pPlayer->SetParentCell(pCell);

    //Each time a player joins create a spare dummy marker on retainer
    CreateDummyMarker();
}

void PlayerService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    auto it = m_mapHandles.begin();
    for (; it != m_mapHandles.end(); it++)
    {
        if (it->PlayerId == acMessage.PlayerId)
        {
            TESObjectREFR* dummyPlayer = TESObjectREFR::GetByHandle(it->handle);
            dummyPlayer->position.x = -INTMAX_MAX;
            dummyPlayer->position.y = -INTMAX_MAX;
            PlayerCharacter::Get()->RemoveMapmarkerRef(it->handle);
            break;
        }
    }

    if (it == m_mapHandles.end())
    {
        spdlog::error(__FUNCTION__ ": could not find player id {:X}", acMessage.PlayerId);
        return;
    }

    DeleteMarkerDummy(it->handle);

    m_mapHandles.erase(it);
}

void PlayerService::OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) const noexcept
{
    PlayerCharacter::Get()->PayGold(acMessage.GoldLost);

    std::string message = fmt::format("You died and lost {} gold.", acMessage.GoldLost);
    Utils::ShowHudMessage(String(message));
}

void PlayerService::OnGridCellChangeEvent(const GridCellChangeEvent& acEvent) const noexcept
{
    uint32_t baseId = 0;
    uint32_t modId = 0;

    if (m_world.GetModSystem().GetServerModId(acEvent.WorldSpaceId, modId, baseId))
    {
        ShiftGridCellRequest request;
        request.WorldSpaceId = GameId(modId, baseId);
        request.PlayerCell = acEvent.PlayerCell;
        request.CenterCoords = acEvent.CenterCoords;
        request.Cells = acEvent.Cells;

        m_transport.Send(request);
    }
}

void PlayerService::OnCellChangeEvent(const CellChangeEvent& acEvent) const noexcept
{
    if (acEvent.WorldSpaceId)
    {
        EnterExteriorCellRequest message;
        message.CellId = acEvent.CellId;
        message.WorldSpaceId = acEvent.WorldSpaceId;
        message.CurrentCoords = acEvent.CurrentCoords;

        m_transport.Send(message);
    }
    else
    {
        EnterInteriorCellRequest message;
        message.CellId = acEvent.CellId;

        m_transport.Send(message);
    }
}

void PlayerService::OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    const auto& partyService = m_world.GetPartyService();
    if (!partyService.IsInParty() || !partyService.IsLeader())
        return;

    PlayerDialogueRequest request{};
    request.Text = acEvent.Text;

    m_transport.Send(request);
}

void PlayerService::OnMapOpen(const MapOpenEvent& acMessage) noexcept
{

}

void PlayerService::OnMapClose(const MapCloseEvent& acMessage) noexcept
{
    if (!m_transport.IsConnected() || m_waypointActive || m_waypoint->position.x == -INTMAX_MAX)
        return;

    SetWaypoint(PlayerCharacter::Get(), &m_waypoint->position, PlayerCharacter::Get()->GetWorldSpace());
}


void PlayerService::OnNotifyPlayerPosition(const NotifyPlayerPosition& acMessage) const noexcept
{   
    auto it = m_mapHandles.begin();
    for (; it != m_mapHandles.end(); it++)
    {
        if (it->PlayerId == acMessage.PlayerId)
        {
            break;
        }
    }

    if (it == m_mapHandles.end())
    {
        spdlog::error(__FUNCTION__ ": could not find player id {:X}", acMessage.PlayerId);
        return;
    }

    auto* pDummyPlayer = TESObjectREFR::GetByHandle(it->handle);
    if (!pDummyPlayer)
    {
        spdlog::error(__FUNCTION__ ": could not find dummy player, handle: {:X}", it->handle);
        return;
    }
    
    ExtraMapMarker* pMapMarker = Cast<ExtraMapMarker>(pDummyPlayer->extraData.GetByType(ExtraData::MapMarker));
    if (!pMapMarker || !pMapMarker->pMarkerData)
    {
        spdlog::error(__FUNCTION__ ": could not find map marker data, player id: {:X}", acMessage.PlayerId);
        return;
    }

    MapMarkerData* pMarkerData = pMapMarker->pMarkerData;

    // TODO: this is flawed due to cities being worldspaces on the same map
    auto* pDummyWorldSpace = pDummyPlayer->GetWorldSpace();
    auto* pPlayerWorldSpace = PlayerCharacter::Get()->GetWorldSpace();

    if (pDummyPlayer->IsInInteriorCell() || 
        (pPlayerWorldSpace && pDummyWorldSpace != pPlayerWorldSpace) ||
        (pDummyWorldSpace && pDummyWorldSpace != pPlayerWorldSpace))
    {
        pMarkerData->cOriginalFlags = pMarkerData->cFlags = MapMarkerData::Flag::NONE;
        return;
    }

    pMarkerData->cOriginalFlags = pMarkerData->cFlags = MapMarkerData::Flag::VISIBLE | MapMarkerData::Flag::CAN_TRAVEL_TO;

    pDummyPlayer->position = acMessage.Position;
}

void PlayerService::OnNotifyPlayerCellChanged(const NotifyPlayerCellChanged& acMessage) const noexcept
{
    auto it = m_mapHandles.begin();
    for (; it != m_mapHandles.end(); it++)
    {
        if (it->PlayerId == acMessage.PlayerId)
        {
            break;
        }
    }

    if (it == m_mapHandles.end())
    {
        spdlog::error(__FUNCTION__ ": could not find player id {:X}", acMessage.PlayerId);
        return;
    }

    TESObjectCELL* pCell = GetCell(acMessage.CellId, acMessage.WorldSpaceId, acMessage.CenterCoords);
    if (!pCell)
    {
        spdlog::error(__FUNCTION__ ": could not find cell {:X}", acMessage.CellId.BaseId);
        return;
    }

    auto* pDummyPlayer = TESObjectREFR::GetByHandle(it->handle);
    if (!pDummyPlayer)
    {
        spdlog::error(__FUNCTION__ ": could not find dummy player, handle: {:X}", it->handle);
        return;
    }

    pDummyPlayer->SetParentCell(pCell);
}

// on join/leave, add to our array...
void PlayerService::OnPlayerMapMarkerUpdateEvent(const PlayerMapMarkerUpdateEvent& acEvent) const noexcept
{

}

void PlayerService::OnPlayerLevelEvent(const PlayerLevelEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerLevelRequest request{};
    request.NewLevel = PlayerCharacter::Get()->GetLevel();

    m_transport.Send(request);
}

void PlayerService::OnPlayerSetWaypoint(const PlayerSetWaypointEvent& acMessage) noexcept
{

    if (!m_transport.IsConnected())
        return;

    m_waypointActive = true;
    m_waypoint->position.x = -INTMAX_MAX;
    m_waypoint->position.y = -INTMAX_MAX;

    RequestSetWaypoint request = {};
    request.Position = acMessage.Position;
    m_transport.Send(request);

    NiPoint3 Position = {};
    Position.x = acMessage.Position.x;
    Position.y = acMessage.Position.y;
    auto* pPlayerWorldSpace = PlayerCharacter::Get()->GetWorldSpace();
    SetWaypoint(PlayerCharacter::Get(), &Position, pPlayerWorldSpace);
}

void PlayerService::OnPlayerDelWaypoint(const PlayerDelWaypointEvent& acMessage) noexcept
{
    if (!m_transport.IsConnected())
        return;

    m_waypointActive = false;

    RequestDelWaypoint request = {};
    m_transport.Send(request);
}

void PlayerService::OnNotifyPlayerDelWaypoint(const NotifyDelWaypoint& acMessage) noexcept
{
    m_waypoint->position.x = -INTMAX_MAX;
    m_waypoint->position.y = -INTMAX_MAX;
}

void PlayerService::OnNotifyPlayerSetWaypoint(const NotifySetWaypoint& acMessage) noexcept
{
    if (!m_inMap)
    {
        NiPoint3 pos{};
        pos.x = acMessage.Position.x;
        pos.y = acMessage.Position.y;
        SetWaypoint(PlayerCharacter::Get(), &pos, PlayerCharacter::Get()->GetWorldSpace());
        return;
    }
    m_waypointActive = false;
    RemoveWaypoint(PlayerCharacter::Get());
    m_waypoint->position = acMessage.Position;
}

void PlayerService::OnNotifyPlayerPosition(const NotifyPlayerPosition& acMessage) const noexcept
{
    MapMenu* pMapMenu = reinterpret_cast<decltype(pMapMenu)>(UI::Get()->FindMenuByName("MapMenu"));
    spdlog::info(pMapMenu == nullptr);
    SetWaypoint(pMapMenu, acMessage.Position.x, acMessage.Position.y);
}

// on join/leave, add to our array...
void PlayerService::OnPlayerMapMarkerUpdateEvent(const PlayerMapMarkerUpdateEvent& acEvent) const noexcept
{
    // for only players that are in the same worldspace as we are...
    for (int32_t handle : m_ownedMaphandles)
    {
        #if 0
        // fetch smart pointer from handle function
        // then fetch all players,
        if worldspace = remoteworldspace

            #endif
    }
}
void PlayerService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    // TODO: this can be done a bit prettier
#if TP_SKYRIM64
    if (acEvent.IsLeader)
    {
        TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
        pWorldEncountersEnabled->f = 1.f;
    }
#elif TP_FALLOUT4
        // TODO: ft
#endif
}

void PlayerService::OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept
{
    m_waypointData->cOriginalFlags = m_waypointData->cFlags = MapMarkerData::Flag::VISIBLE;
    m_waypoint->position.x = acMessage.Position.x;
    m_waypoint->position.y = acMessage.Position.y;

    // TODO: this can be done a bit prettier
#if TP_SKYRIM64
    if (World::Get().GetTransport().IsConnected())
    {
        TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
        pWorldEncountersEnabled->f = 0.f;
    }
#elif TP_FALLOUT4
        // TODO: ft
#endif
}

// TODO: ft (verify)
void PlayerService::RunRespawnUpdates(const double acDeltaTime) noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    static bool s_startTimer = false;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer->actorState.IsBleedingOut())
    {
#if TP_SKYRIM64
        m_cachedMainSpellId = pPlayer->magicItems[0] ? pPlayer->magicItems[0]->formID : 0;
        m_cachedSecondarySpellId = pPlayer->magicItems[1] ? pPlayer->magicItems[1]->formID : 0;
        m_cachedPowerId = pPlayer->equippedShout ? pPlayer->equippedShout->formID : 0;
#endif

        s_startTimer = false;
        return;
    }

    if (!s_startTimer)
    {
        s_startTimer = true;
        m_respawnTimer = 5.0;
        FadeOutGame(true, true, 3.0f, true, 2.0f);

        // If a player dies not by its health reaching 0, getting it up from its bleedout state isn't possible
        // just by setting its health back to max. Therefore, put it to 0.
        if (pPlayer->GetActorValue(ActorValueInfo::kHealth) > 0.f)
            pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, 0);

        pPlayer->PayCrimeGoldToAllFactions();
    }

    m_respawnTimer -= acDeltaTime;

    if (m_respawnTimer <= 0.0)
    {
        pPlayer->RespawnPlayer();

        m_knockdownTimer = 1.5;
        m_knockdownStart = true;

        m_transport.Send(PlayerRespawnRequest());

        s_startTimer = false;

#if TP_SKYRIM64
        auto* pEquipManager = EquipManager::Get();
        TESForm* pSpell = TESForm::GetById(m_cachedMainSpellId);
        if (pSpell)
            pEquipManager->EquipSpell(pPlayer, pSpell, 0);
        pSpell = TESForm::GetById(m_cachedSecondarySpellId);
        if (pSpell)
            pEquipManager->EquipSpell(pPlayer, pSpell, 1);
        pSpell = TESForm::GetById(m_cachedPowerId);
        if (pSpell)
            pEquipManager->EquipShout(pPlayer, pSpell);
#endif
    }
}

// TODO: ft (verify)
// Doesn't seem to respawn quite yet
void PlayerService::RunPostDeathUpdates(const double acDeltaTime) noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    // If a player dies in ragdoll, it gets stuck.
    // This code ragdolls the player again upon respawning.
    // It also makes the player invincible for 5 seconds.
    if (m_knockdownStart)
    {
        m_knockdownTimer -= acDeltaTime;
        if (m_knockdownTimer <= 0.0)
        {
            PlayerCharacter::SetGodMode(true);
            m_godmodeStart = true;
            m_godmodeTimer = 10.0;

            PlayerCharacter* pPlayer = PlayerCharacter::Get();
            pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);

            FadeOutGame(false, true, 0.5f, true, 2.f);

            m_knockdownStart = false;
        }
    }

    if (m_godmodeStart)
    {
        m_godmodeTimer -= acDeltaTime;
        if (m_godmodeTimer <= 0.0)
        {
            PlayerCharacter::SetGodMode(false);

            m_godmodeStart = false;
        }
    }
}

void PlayerService::RunDifficultyUpdates() const noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerCharacter::Get()->SetDifficulty(m_serverDifficulty);
}

void PlayerService::RunLevelUpdates() const noexcept
{
    // The LevelUp hook is kinda weird, so ehh, just check periodically, doesn't really cost anything.

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    static uint16_t oldLevel = PlayerCharacter::Get()->GetLevel();

    uint16_t newLevel = PlayerCharacter::Get()->GetLevel();
    if (newLevel != oldLevel)
    {
        PlayerLevelRequest request{};
        request.NewLevel = newLevel;

        m_transport.Send(request);

        oldLevel = newLevel;
    }
}

void PlayerService::RunMapUpdates() noexcept
{

    // Update map open status
    const VersionDbPtr<int> inMapAddr(403437);
    int* inMap = reinterpret_cast<decltype(inMap)>(inMapAddr.Get());

    // Map Open/Close
    if (*inMap != m_inMap)
    {
        switch (*inMap)
        {

            // Map was closed
            case 0:
                World::Get().GetRunner().Trigger(MapCloseEvent());

            // Map was opened
            case 1:
                World::Get().GetRunner().Trigger(MapOpenEvent());
        }
    }

    m_inMap = *inMap == 1;
}

void PlayerService::ToggleDeathSystem(bool aSet) noexcept
{
    m_isDeathSystemEnabled = aSet;

    PlayerCharacter::Get()->SetPlayerRespawnMode(aSet);
}
