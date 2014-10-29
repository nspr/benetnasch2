#include "blib.hpp"
#include "bengine.hpp"
#include "physics.hpp"
#include "maps.hpp"
#include "rendering.hpp"
#include "components/gamecomponents.hpp"
#include "components/player.hpp"
#include "input.hpp"
#include "network.hpp"
#include "netconst.hpp"
#include "client/clientdata.hpp"
#include "client/think.hpp"
#include "physics.hpp"

bool sys_init()
{
    Sys::afont = new bfont(Sys::Renderer, std::string("The Strider.bmp"));
    
    #ifndef B_DEBUG_COREFRAMES
        Maps::load_wallmask("wallmask.png");
        Maps::load_background("background.png");
        
        Sys::myinput.Init();
        Sys::myself = nullptr;
        Sys::did_send_playerrequest = false;
    #endif
    
    Sys::tems.push_back(&Sys::FrameLimit); // bengine
    Sys::tems.push_back(&Net::think);
    #ifndef B_DEBUG_COREFRAMES
        Sys::tems.push_back(&Sys::UpdateDelta); // physics
    #endif
    Sys::tems.push_back(&Sys::SDLEvents); // bengine
    #ifndef B_DEBUG_COREFRAMES
        Sys::tems.push_back(&Sys::ClientThink); // client/think
        Sys::tems.push_back(&Sys::Physics); // physics
    #endif
    Sys::tems.push_back(&Sys::RenderThings); // rendering
    Sys::tems.push_back(&Sys::PresentScreen); // rendering
    
    return 1;
}

void process_message_playerinput(Net::Connection * connection, double buffer)
{
    if(Sys::myself->physics_frames_since_input_cycle)
    {
        Sys::myself->input.cycleInput();
        Sys::myself->physics_frames_since_input_cycle = 0;
    }
    Sys::myself->input.setInputsAsBitfield ( read_ushort(buffer) ) ;
    Sys::myself->input.aimDirection = read_ushort(buffer)*360.0/0x10000;
    Sys::myself->input.aimDistance = read_ubyte(buffer)*2;
}

void process_message_spawnnewplayer(Net::Connection * connection, double buffer)
{
    puts("spawning new player");
    auto player = new Sys::Player(Ent::New(), read_string(buffer, read_ubyte(buffer)));
    
    player->spawn(read_ushort(buffer), read_ushort(buffer));
    player->character->myself = read_ubyte(buffer);
    if(player->character->myself)
        Sys::myself = player;
}

bool main_init()
{
	Sys::speeds.push_back(0);
	
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
        std::cout << "Could not initialize SDL: " << SDL_GetError() << std::endl;
    
    Sys::MainWindow = SDL_CreateWindow("Benetnasch", 300, 300, Sys::shape.w, Sys::shape.h, SDL_WINDOW_SHOWN);
    if (Sys::MainWindow == nullptr)
        std::cout << "Could not create an SDL window: " << SDL_GetError() << std::endl;
    Sys::Renderer = SDL_CreateRenderer(Sys::MainWindow, -1, SDL_RENDERER_ACCELERATED);
    if (Sys::Renderer == nullptr)
        std::cout << "Could not create an SDL renderer: " << SDL_GetError() << std::endl;
    
    srand(time(NULL));
    
    SDL_PumpEvents();
    Sys::myinput.Init();
    
    if(faucnet_init() < 0)
        return 0;
    
    Sys::view_x = 0;
    Sys::view_y = 0;
    
    Net::init(0);
    
    Sys::server = new Net::Connection( "127.0.0.1", 4192 );
    Net::connections.push_back(Sys::server);
    Sys::server->send_or_resend_connection_request();
    
    Net::assign ( 1, SERVERMESSAGE::PLAYERINPUT, &process_message_playerinput );
    Net::assign ( 0, SERVERMESSAGE::SPAWNNEWPLAYER, &process_message_spawnnewplayer );
    
    Sys::tems.push_back(&sys_init);
    
    return 0;
}
