#include "SaveStates.h"
#include "SaveStateCodec.h"
#include "Bridge.h"
#include "GameLobbyController.h"
#include "Savegame.h"
#include "Settings.h"
#include "RttrConfig.h"
#include "WindowManager.h"
#include "files.h"
#include "desktops/dskGameInterface.h"
#include "desktops/dskGameLoader.h"
#include "desktops/dskMainMenu.h"
#include "network/ClientInterface.h"
#include "network/CreateServerInfo.h"
#include "network/GameClient.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldView.h"
#include <boost/filesystem.hpp>
#include <chrono>
#include <fstream>
#include <memory>

namespace {
namespace fs=boost::filesystem;
fs::path directory;
SavedState pending;
enum class Phase { Idle, Queued, Loading };
Phase phase=Phase::Idle;
std::chrono::steady_clock::time_point started;
bool loadError=false;
void removeFile(const fs::path& path) { boost::system::error_code ec;fs::remove(path,ec); }
fs::path scratch() { return directory/"libretro-state.sav"; }
class RestoreClient final: public ClientInterface {
    void start() {
        GAMECLIENT.Command_SetReady(true);
        GameLobbyController(GAMECLIENT.GetGameLobby(),GAMECLIENT.GetMainPlayer()).StartCountdown(0);
    }
public:
    ~RestoreClient() override { GAMECLIENT.RemoveInterface(this); }
    void CI_NextConnectState(ConnectState state) override {
        if(state!=ConnectState::Finished)return;
        if(GAMECLIENT.GetPlayerId()!=pending.player)
            GameLobbyController(GAMECLIENT.GetGameLobby(),GAMECLIENT.GetMainPlayer()).SwapPlayers(GAMECLIENT.GetPlayerId(),pending.player);
        else start();
    }
    void CI_PlayersSwapped(unsigned,unsigned) override {
        if(GAMECLIENT.GetPlayerId()==pending.player)start();
    }
    void CI_GameLoading(std::shared_ptr<Game> game) override {
        WINDOWMANAGER.Switch(std::make_unique<dskGameLoader>(std::move(game)));
    }
    void CI_Error(ClientError) override { loadError=true; }
};
std::unique_ptr<RestoreClient> restoreClient;
}

void ConfigureSaveStates(const boost::filesystem::path& value) {
    directory=value;
    fs::create_directories(directory);
}
bool StateLoadPending() { return phase!=Phase::Idle; }
void ClearPendingStateLoad() {
    restoreClient.reset();
    phase=Phase::Idle;
    pending=SavedState{};
    if(!directory.empty()) {
        removeFile(scratch());
        removeFile(RTTRCONFIG.ExpandPath(s25::folders::mapsPlayed)/scratch().filename());
    }
}
bool SerializeGame(void* output,size_t size) {
    auto* desktop=dynamic_cast<dskGameInterface*>(WINDOWMANAGER.GetCurrentDesktop());
    if(!output||size<StateCapacity||StateLoadPending()||!desktop||GAMECLIENT.GetState()!=ClientState::Game
       ||GAMECLIENT.IsReplayModeOn()||!desktop->LibretroView().GetWorld().IsSinglePlayer())return false;
    try {
        const auto began=std::chrono::steady_clock::now();
        auto& view=desktop->LibretroView();
        SavedState state;
        state.gameFrame=GAMECLIENT.GetGFNumber();state.player=GAMECLIENT.GetPlayerId();
        state.x=view.GetOffset().x;state.y=view.GetOffset().y;
        state.zoom=view.GetCurrentTargetZoomFactor();state.hud=view.GetHudFlags();state.paused=GAMECLIENT.IsPaused();
        if(!GAMECLIENT.SaveToFile(scratch(),false)) {removeFile(scratch());return false;}
        const auto length=fs::file_size(scratch());
        if(!length||length>StateCapacity-48) {removeFile(scratch());return false;}
        state.payload.resize(static_cast<size_t>(length));
        std::ifstream file(scratch().string(),std::ios::binary);
        file.read(reinterpret_cast<char*>(state.payload.data()),state.payload.size());
        const bool read=bool(file);file.close();removeFile(scratch());
        if(!read)return false;
        const bool encoded=encodeState(state,output,size);
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count();
        rttr_log(("State saved at game frame "+std::to_string(state.gameFrame)+" in "+std::to_string(elapsed)+" ms").c_str());
        return encoded;
    } catch(const std::exception& error) {rttr_log(error.what());removeFile(scratch());return false;}
}
bool UnserializeGame(const void* data,size_t size) {
    if(directory.empty()||StateLoadPending())return false;
    SavedState state;
    if(!decodeState(data,size,state))return false;
    try {
        std::ofstream file(scratch().string(),std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(state.payload.data()),state.payload.size());
        file.close();
        if(!file) {removeFile(scratch());return false;}
        // Reject damaged or incompatible native files before ending the current game.
        Savegame check;
        if(!check.Load(scratch(),SaveGameDataToLoad::All)||check.start_gf!=state.gameFrame||state.player>=check.GetNumPlayers()) {
            removeFile(scratch());return false;
        }
        state.payload.clear();
        pending=std::move(state);phase=Phase::Queued;loadError=false;
        return true;
    } catch(const std::exception& error) {rttr_log(error.what());removeFile(scratch());return false;}
}
void RunPendingStateLoad() {
    if(phase==Phase::Idle)return;
    try {
        if(phase==Phase::Queued) {
            // World/resource replacement needs the frontend's active GL context.
            WINDOWMANAGER.CleanUp();
            GAMECLIENT.Stop();
            WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
            restoreClient=std::make_unique<RestoreClient>();
            GAMECLIENT.SetInterface(restoreClient.get());
            started=std::chrono::steady_clock::now();phase=Phase::Loading;
            if(!GAMECLIENT.HostGame(CreateServerInfo(ServerType::Local,SETTINGS.server.localPort,"RetroArch state"),
                                    {scratch(),MapType::Savegame}))loadError=true;
        } else if(GAMECLIENT.GetState()==ClientState::Game) {
            if(auto* desktop=dynamic_cast<dskGameInterface*>(WINDOWMANAGER.GetCurrentDesktop())) {
                auto& view=desktop->LibretroView();
                view.MoveTo(DrawPoint(pending.x,pending.y));
                view.SetZoomFactor(pending.zoom,false);view.RestoreHudFlags(pending.hud);
                if(pending.paused)GAMECLIENT.SetPause(true);
                rttr_log(("State restored at game frame "+std::to_string(GAMECLIENT.GetGFNumber())).c_str());
                ClearPendingStateLoad();return;
            }
        }
        if(loadError||std::chrono::steady_clock::now()-started>std::chrono::seconds(45)) {
            ClearPendingStateLoad();WINDOWMANAGER.CleanUp();GAMECLIENT.Stop();
            WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
            rttr_message("Spielstand konnte nicht geladen werden.");
        }
    } catch(const std::exception& error) {
        rttr_log(error.what());ClearPendingStateLoad();
        WINDOWMANAGER.CleanUp();GAMECLIENT.Stop();WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>());
        rttr_message("Fehler beim Laden des Spielstands.");
    }
}
