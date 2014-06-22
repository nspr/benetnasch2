#include <iostream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <functional>
#include <cmath>

#include <SDL2/SDL.h>
#undef main
#include <SDL2/SDL_image.h>
#include <nall/set.hpp>
#include "bmath.cpp"
#include "btime.cpp"
#include "sdlhelpers.cpp"

typedef long long entityid_t;

namespace Ent
{
    nall::set<entityid_t> Used; // IDs which are currently being used
    nall::set<entityid_t> Freed; // IDs which were used at some point, but no longer are
    entityid_t LowestUnused = 0; // The lowest ID which has never been used yet
    
    entityid_t New()
    {
        if (Freed.size() == 0) // No freed IDs, use lowest unused
        {
            auto id = LowestUnused;
            Used.insert(id); //logn
            
            #if DEBUG_IDALLOCATOR
                if(Used.find(LowestUnused)) // logn
                {
                    std::cout << "Error: Former LowestUnused set was invalid!";
                    return -1;
                }
            #endif
            
            // We need to figure out what the next lowest unused will be
            // There aren't any freed ones to use, so we have to do it hard
            if (LowestUnused >= *Used.end()) // lowest unused is outside of used range, free to increment LowestUnused
                LowestUnused++;
            else // This shouldn't ever happen; no freed IDs, lowest unused being inside Used's bounds -- implies earlier error setting LowestUnused
            {
                std::cout << "Error: Unknown cause with LowestUnused being invalid 1!";
                return -1;
            }
            return id;
        }
        else // use an ID from the freed list
        {
            auto id = *Freed.begin();
            Freed.remove(id); //logn
            Used.insert(id); //logn
            
            if(Freed.size() > 0) // We have an ID to use
            { // (Freed.size == 0 block won't be the next to run if we're here, so...)
                LowestUnused = *Freed.begin();
                return id;
            }
            else // We just exhausted the last previously freed ID
            {
                if (LowestUnused >= *Used.end()) // lowest unused is outside of used range, free to increment LowestUnused
                    LowestUnused++;
                else // This shouldn't ever happen; no freed IDs, lowest unused being inside Used's bounds -- implies earlier error setting LowestUnused
                {
                    std::cout << "Error: Unknown cause with LowestUnused being invalid 2!";
                    return -1;
                }
                return id;
            }
        }
    }
    // free a given ID -- returns false if id doesn't exist
    bool Free(entityid_t id) //perf: 3xlogn
    {
        if(Used.find(id))
            return false;
        Freed.insert(id);
        Used.remove(id);
        if(id < LowestUnused)
            LowestUnused = id;
        return true;
    }
    bool Exists(entityid_t id)
    {
        return Used.find(id);
    }
}

namespace Input
{
    enum {
        JUMP,
        LEFT,
        RIGHT,
        DOWN
    };
    bool inputs[256] = { }; 
    bool last_inputs[256] = { }; 
    signed char scrolls[4];
    const Uint8 * corestate;
    
    struct keybinding
    {
        SDL_Scancode key;
        unsigned char input_index;
    };
    std::vector<keybinding> keybindings;
    struct mousebinding
    {
        SDL_Scancode button;
        unsigned char input_index;
    };
    std::vector<mousebinding> mousebindings;
    
    void Init()
    {
        corestate = SDL_GetKeyboardState(NULL);
        keybindings.push_back({SDL_SCANCODE_E, JUMP});
        keybindings.push_back({SDL_SCANCODE_W, LEFT});
        keybindings.push_back({SDL_SCANCODE_D, DOWN});
        keybindings.push_back({SDL_SCANCODE_F, RIGHT});
    }
    
    void Update()
    {
        for (short i = 0; i < 256; i++)
        {
            last_inputs[i] = inputs[i];
            inputs[i] = 0;
        }
        for (auto bind : keybindings)
        {
            if(corestate[bind.key])
                inputs[bind.input_index] = true;
        }
        for (auto bind : mousebindings)
        {
            if(corestate[bind.button])
                inputs[bind.input_index] = true;
        }
    }
}

namespace Sys
{
    SDL_Window * MainWindow;
    SDL_Renderer * Renderer;
    SDL_Rect shape = {0, 0, 800, 600};
    
    bool quit = false;
    
    std::vector<bool(*)()> tems; // Sys::tems
    
    struct Component
    {
        Component(entityid_t myEntity);
        virtual ~Component();
        
        entityid_t entityID;
    };

    Component::Component(entityid_t myEntity) : entityID(myEntity)
    { }
    Component::~Component()
    { }
    
    // define collection and interfaces for components of some arbitrary type
    template<typename CType>
    struct Collection
    {
        nall::set<CType*> List;
        
        typename nall::set<CType*>::iterator begin()
        {
            return List.begin();
        }
        typename nall::set<CType*>::iterator end()
        {
            return List.end();
        }
        void add(CType * new_instance)
        {
            List.insert(new_instance);
        }
        void remove(CType * dying_instance)
        {
            List.remove(dying_instance);
        }
    };
    
    // Position component
    struct Position : public Component
    {
        Position(entityid_t myEntity);
        ~Position();
        double x, y;
    };
    // collection of positions
    Collection<Position> Positions;
    // Constructor will add this new instance to its list
    Position::Position(entityid_t myEntity) : Component(myEntity), x(0), y(0)
    {
        Positions.add(this);
    }
    Position::~Position()
    {
        Positions.remove(this);
    }
    
    // Hull component
    struct Hull : public Component
    {
        Hull(entityid_t myEntity);
        ~Hull();
        double h, w, xoffset, yoffset;
    };
    Collection<Hull> Hulls;
    Hull::Hull(entityid_t myEntity) : Component(myEntity), h(0), w(0), xoffset(0), yoffset(0)
    {
        Hulls.add(this);
    }
    Hull::~Hull()
    {
        Hulls.remove(this);
    }
    
    // Sprite component
    struct TexturedDrawable : public Component
    {
        TexturedDrawable(entityid_t myEntity);
        ~TexturedDrawable();
        Position * position;
        SDL_Texture * sprite;
        double xoffset, yoffset;
        bool init(const char * sarg);
    };
    Collection<TexturedDrawable> TexturedDrawables;
    TexturedDrawable::TexturedDrawable(entityid_t myEntity) : Component(myEntity), sprite(NULL), xoffset(0), yoffset(0)
    {
        position = new Position(myEntity);
        TexturedDrawables.add(this);
    }
    TexturedDrawable::~TexturedDrawable()
    {
        delete position;
        SDL_DestroyTexture(sprite);
        
        TexturedDrawables.remove(this);
    }
    bool TexturedDrawable::init(const char * sarg)
    {
        sprite = loadTexture( sarg, Sys::Renderer );
        return sprite != nullptr;
    }
    // Character component
    struct Character : public Component
    {
        Character(entityid_t myEntity);
        ~Character();
        Position * position;
        Hull * hull;
        TexturedDrawable * sprite;
        double hspeed, vspeed;
        bool myself;
        
        void center_on(float x, float y)
        {
            position->x = x - hull->w/2;
            position->y = y - hull->h/2;
        }
        float center_x ()
        {
            return position->x + hull->w/2;
        }
        float center_y ()
        {
            return position->y + hull->h/2;
        }
    };
    Collection<Character> Characters;
    Character::Character(entityid_t myEntity) : Component(myEntity), hspeed(0), vspeed(0), myself(false)
    {
        hull = new Hull(myEntity);
        hull->h = 48;
        hull->w = 32;
        position = new Position(myEntity);
        sprite = new TexturedDrawable(myEntity);
        sprite->init("sprites/mychar.png");
        auto dangle = sprite->position;
        sprite->position = position;
        delete dangle;
        
        Characters.add(this);
    }
    Character::~Character()
    {
        delete hull;
        delete position;
        delete sprite;
        Characters.remove(this);
    }
    
    struct BoxDrawable : public Component
    {
        BoxDrawable(entityid_t myEntity);
        ~BoxDrawable();
        Position * position;
        Hull * hull;
        double xoffset, yoffset;
        
        SDL_Rect* getShape(float x, float y);
        
        private:
            SDL_Rect shape;
    };
    Collection<BoxDrawable> BoxDrawables;
    BoxDrawable::BoxDrawable(entityid_t myEntity) : Component(myEntity)
    {
        position = new Position(myEntity);
        hull = new Hull(myEntity);
        BoxDrawables.add(this);
    }
    BoxDrawable::~BoxDrawable()
    {
        delete position;
        delete hull;
        BoxDrawables.remove(this);
    }
    SDL_Rect * BoxDrawable::getShape(float x, float y) // offsets
    {
        shape = {int(ceil(position->x+hull->xoffset-x)),
                 int(ceil(position->y+hull->yoffset-y)),
                 int(round(hull->w)),
                 int(round(hull->h))};
        return &shape;
    }
    
    bool FrameLimit()
    {
        // Frame limit
        double TimeWaitS;
        double TimeSpent;
        double prehalttime = Time::get_us(); // naively avoid error in non-critical debug code
        double halttime;
        
        if( Time::dostart )
        {
            SDL_Delay(1000.0/Time::Framerate);
            Time::dostart = false;
            Time::deviance = 0;
            halttime = Time::get_us();
        }
        else
        {
            // get time before halting
            // check time between end of last frame limit delay and current code
            TimeSpent = Time::get_us() - Time::simstart_us;
            // calculate desired amount to wait
            TimeWaitS = maximum(0.0, (1000.0 / Time::Framerate) - TimeSpent/1000.0 - Time::deviance);
            
            // store time for delay deviance check
            prehalttime = Time::get_us();
            SDL_Delay(round(TimeWaitS));
            
            // measure current time directly after frame limit delay
            halttime = Time::get_us();
            
            Time::error = (halttime - prehalttime)/1000.0 - round(TimeWaitS);
            // calculate amount of time that halt was inaccurate by and store it for the next iteration to compensate for
            Time::deviance = (halttime - prehalttime)/1000.0 - TimeWaitS;
            
            // incrememt tick count
            Time::ticks = fmod(Time::ticks + 1.0, Time::Framerate);
        }
        // push timings to buffer
        Time::last_us = Time::simstart_us;
        Time::delta_us = Time::simstart_us - Time::last_us;
        Time::delta = Time::delta_us * Time::scale;
        Time::simstart_us = halttime;
        Time::frames.push_back( Time::simstart_us );
        
        // Throw away old timings
        while ( Time::frames.size() > Time::Framesnum )
            Time::frames.erase( Time::frames.begin() );
        
        if(false) // debug
        {
            std::cout << std::fixed << std::setprecision(4)
                      <<"\rfps "    << Time::scale / ((Time::frames.back() - Time::frames.front())/(Time::frames.size()-1))
                      << " sim "    << std::setprecision(2)<< TimeSpent / 1000
                      //<< " bad "    << (Time::last - Time::simstart_us) * 1000
                      //<< " ask "    << std::setprecision(3) << TimeWaitS
                      << " halt "   << (halttime-prehalttime) / 1000
                      //<< " miss "   << std::setprecision(0) << Time::deviance * 1000
                      << " dev "    << Time::deviance << "\n";
        }
        return false;
    }
    bool SDLEvents()
    {
        SDL_PumpEvents();
        SDL_Event event;
        while ( SDL_PollEvent( &event ) )
        {
            switch ( event.type )
            {
            case SDL_QUIT:
                quit = true;
                break;
            }   
        }
        Input::Update();
        return false;
    }
    namespace Physicsers
    {
        /* not systems */
        bool place_meeting (Character * character, float x, float y)
        {
            bool collided = false;
            for(auto wallchunk : Sys::BoxDrawables)
            {
                if(aabb_overlap(wallchunk->position->x                         , wallchunk->position->y                         ,
                                wallchunk->position->x     + wallchunk->hull->w, wallchunk->position->y     + wallchunk->hull->h,
                                character->position->x + x                     , character->position->y + y                     ,
                                character->position->x + x + character->hull->w, character->position->y + y + character->hull->h))
                {
                    collided = true;
                    break;
                }
            }
            return collided;
        }
        std::vector<BoxDrawable*> place_meeting_which (float x1, float y1, float x2, float y2, float x, float y)
        {
            std::vector<BoxDrawable*> overlaps;
            for(auto wallchunk : Sys::BoxDrawables)
            {
                if(aabb_overlap(wallchunk->position->x                   , wallchunk->position->y                   ,
                                wallchunk->position->x+wallchunk->hull->w, wallchunk->position->y+wallchunk->hull->h,
                                x1, y1,
                                x2, y2))
                {
                    overlaps.push_back(wallchunk);
                }
            }
            return overlaps;
        }
        /*
        std::vector<BoxDrawable*> line_overlapping_which (float x1, float y1, float x2, float y2)
        {
            std::vector<BoxDrawable*> overlaps;
            for(auto wallchunk : Sys::BoxDrawables)
            {
                auto place_meeting = [wallchunk](float x, float y)
                {
                    return aabb_overlap(wallchunk->position->x, wallchunk->position->y,
                                        wallchunk->position->x+ wallchunk->hull->w, wallchunk->position->y+ wallchunk->hull->h,
                                        character->position->x + x, character->position->y + y,
                                        character->position->x + x + character->hull->w, character->position->y + y + character->hull->h);
                };
                
                if(place_meeting(x, y))
                {
                    overlaps.push_back(wallchunk);
                }
            }
            return overlaps;
        }*/
        // moves our character to contact with a wall chunk
        // returns square distance traveled
        // algorithm reference image: http://i.imgur.com/tq1rulr.png
        std::tuple<float, float> move_contact (Character * character, double hvec, double vvec)
        {
            if(hvec == 0 and vvec == 0)
            {
                //puts("empty move_contact call");
                return std::tuple<float, float>(0.0f, 0.0f);
            }
            double &width = character->hull->w;
            double &height = character->hull->h;
            auto &x = character->position->x;
            auto &y = character->position->y;
            float xsign = ssign(hvec);
            float ysign = ssign(vvec);
            //printf("input: %f %f %f %f %f %f\n", x, y, width, height, hvec, vvec);
            
            // make a motion bounding rect and include any collision rect that overlaps it
            auto which = place_meeting_which(x+(xsign < 0 ? width : 0)     , y+(ysign < 0 ? height : 0)     ,
                                             x+(xsign > 0 ? width : 0)+hvec, y+(ysign > 0 ? height : 0)+vvec,
                                             hvec, vvec);
            std::cout << which.size() << "\n";
            
            if(which.size() == 0)
            {
                x += hvec;
                y += vvec;
                //puts("no collision at move_contact");
                return std::tuple<float, float>(hvec, vvec);
            }
            else
            {
                auto oldx = x;
                auto oldy = y;
                
                auto red_x = x + hvec + (xsign > 0 ? width  : 0);
                auto red_y = y + vvec + (ysign > 0 ? height : 0);
                //printf("red: %f %f\n", red_x, red_y);
                
                float furthest = 0;
                float rx;
                float ry;
                // loop through each box to eject from and pick the one with the furthest ejection
                for(auto box : which) // this WILL run at least once
                {
                    // not actually used for anything ignore /s
                    auto eject_x = x;
                    auto eject_y = y;
                    
                    // super cool important things that are used for everything
                    auto green_x = box->position->x + (xsign > 0 ? 0 : box->hull->w);
                    auto green_y = box->position->y + (ysign > 0 ? 0 : box->hull->h);
                    
                    auto inner_height = red_y - green_y;
                    auto inner_width  = red_x - green_x;
                    
                    //printf("green and inner: %f %f %f %f\n", green_x, green_y, inner_width, inner_height);
                    
                    float blue_x, blue_y;
                    if(hvec != 0)
                    {
                        blue_y = red_y - (inner_width/hvec)*vvec;
                        if(ssign(blue_y - green_y) == ysign)
                        {
                            blue_x = green_x;
                            //puts("side ejection");
                            //printf("blue: %f %f\n", blue_x, blue_y);
                            goto tail;
                        }
                    }
                    // falls through to here if the blue dot is on the wrong side of green line
                    blue_x = red_x - (inner_height/vvec)*hvec;
                    blue_y = green_y;
                    //puts("nonside ejection");
                    //printf("blue: %f %f\n", blue_x, blue_y);
                    
                    tail:
                    eject_x = blue_x - (xsign > 0 ? width  : 0);
                    eject_y = blue_y - (ysign > 0 ? height : 0);
                    auto eject_sqdist = sqdist(blue_x - red_x, blue_y - red_y); // order doesn't matter because square
                    if(eject_sqdist > furthest)
                    {
                        furthest = eject_sqdist;
                        rx = eject_x;
                        ry = eject_y;
                    }
                }
                x = rx;
                y = ry;
                //printf("moved: %f %f\n", x - oldx, y - oldy);
                return std::tuple<float, float>(x - oldx, y - oldy);
            }
        }
        /* systems */
        bool MoveCharacters()
        {
            for(auto character : Sys::Characters)
            {
                /*
                 *  predef
                 */
                double &x = character->position->x;
                double &y = character->position->y;
                double &hspeed = character->hspeed;
                double &vspeed = character->vspeed;
                double delta = 1/Time::Framerate;
                
                /* set up muh functions */
                int stepsize = 4;
                /*
                 *  handle accelerations
                 */
                
                float taccel = 1800*delta;
                float gravity = 900*delta;
                float max_gravity = 2000;
                float jumpspeed = -300;
                float fric_moving = pow(0.2, delta);
                float fric_counter = pow(0.01, delta);
                float fric_still = pow(0.02, delta);
                int crawlspeed = 75;
                int walkspeed = 170;
                int runspeed = 300;
                float struggle = 0.6;
                
                int direction = (Input::inputs[Input::RIGHT] - Input::inputs[Input::LEFT]);
                int jumping = (Input::inputs[Input::JUMP] & !Input::last_inputs[Input::JUMP]);
                
                // If we're moving too slowly, our acceleration should be dampened
                if (direction == int(sign(hspeed)) or hspeed == 0)
                {
                    if (abs(hspeed) >= walkspeed)
                        ;
                    else if (abs(hspeed) <= crawlspeed)
                        taccel *= struggle;
                    else
                    {
                        float factor =
                          (float(abs(hspeed)) - crawlspeed)
                          / walkspeed;
                        taccel *= lerp(struggle, 1.0f, factor);
                    }
                }
                
                // calculate post-control speed
                float walk_solution;
                if(abs(hspeed) < crawlspeed) // no friction if extremely slow
                    walk_solution = hspeed + taccel*direction;
                else // friction if we're extremely fast
                    walk_solution = (hspeed + taccel*direction) * fric_moving;
                
                // If we're changing directions, we do either friction OR deacceleration; whichever one is stronger
                // this is the counter-friction
                auto fric_solution = hspeed * fric_counter; // same sign
                
                if(direction < 0) // prefer more negative
                    hspeed = minimum(walk_solution, fric_solution);
                if(direction > 0) // more positive
                    hspeed = maximum(walk_solution, fric_solution);
                    
                auto hsign = (0 < hspeed) - (hspeed < 0);
                
                if (abs(hspeed) > float(runspeed))
                    hspeed = hsign * runspeed;
                if (direction == 0)
                {
                    hspeed *= fric_still;
                    hspeed = absolute(hspeed) - 500*delta;
                    hspeed = hspeed > 0 ? hspeed : 0;
                    hspeed *= hsign;
                }
                
                if(!place_meeting(character, 0, crop1(gravity)))
                {
                    vspeed += gravity;
                    if(vspeed > max_gravity)
                        vspeed = max_gravity;
                }
                if(jumping)
                    vspeed = jumpspeed;
                
                /*
                 *  muh movement solving
                 */
                
                hspeed *= delta;
                vspeed *= delta;
                auto h_auto = hspeed;
                auto v_auto = vspeed;
                
                /* movement solver starts here */
                // we're in the wallmask
                if (place_meeting(character, 0, 0))
                {
                    puts("woaaAAAHHHh we're in the wallmask 1!");
                    for (int i = 1; i < stepsize; i += 1)
                    {
                        if(!place_meeting(character, 0, -i))
                        {
                            y -= i;
                            puts("eject up");
                            break;
                        }
                    }
                }
                // we collided with something
                if (place_meeting(character, hspeed, vspeed))
                {
                    puts("rectifying a collision");
                    float mx, my;
                    std::tie(mx, my) = move_contact(character, hspeed, vspeed);
                    std::cout << "move_contact-ed " << vector_length(vspeed, hspeed) << " to " << vector_length(mx, my) << "\n";
                    //if(sqdist(mx, my) > sqdist(vspeed, hspeed))
                    //    throw;
                    h_auto -= mx;
                    v_auto -= my;
                    
                    if(sqdist(mx, my) != sqdist(vspeed, hspeed)) // avoid obscure spatial-temporal aliasing bug
                    {
                        // check for walls
                        if(place_meeting(character, crop1(h_auto), 0))
                        {
                            auto oy = y;
                            // check for slopes
                            for (int i = stepsize; i <= abs(h_auto)+stepsize; i += stepsize)
                            {
                                puts("testing slopes");
                                if(!place_meeting(character, h_auto, i))
                                {
                                    y += i;
                                    puts("downceil");
                                    break;
                                }
                                else if(!place_meeting(character, h_auto, -i))
                                {
                                    y -= i;
                                    puts("upslope");
                                    break;
                                }
                            }
                            // no slopw; wall
                            if(oy == y)
                            {
                                puts("w");
                                hspeed = 0;
                                h_auto = 0;
                            }
                        }
                        // assume floor otherwise
                        else
                        {
                            puts("f");
                            vspeed = 0;
                            v_auto = 0;
                        }
                    }
                }
                // we did not collide with something
                else
                {
                    // we might want to "down" a slope
                    if(vspeed >= 0)
                    {
                        for (int i = stepsize; i <= abs(h_auto)+stepsize; i += stepsize)
                        {
                            if(!place_meeting(character, h_auto, i) and place_meeting(character, h_auto, i+1))
                            {
                                puts("downslope");
                                y += i;
                                vspeed = 0;
                                v_auto = 0;
                                break;
                            }
                        }
                    }
                }
                
                x += h_auto;
                y += v_auto;
                if (place_meeting(character, 0, 0))
                    puts("woaaAAAHHHh we're in the wallmask 2!");
                
                hspeed /= delta;
                vspeed /= delta;
                puts("end frame");
            };
            return false;
        }
    }
    bool Physics()
    {
        Physicsers::MoveCharacters();
        return false;
    }
    namespace Renderers
    {
        bool TexturedDrawables(float x, float y) // topleft corner position
        {
            for(auto drawable : Sys::TexturedDrawables)
            {
                renderTexture( drawable->sprite, Sys::Renderer, drawable->position->x-x, drawable->position->y-y );
            };
            return false;
        }
        bool BoxDrawables(float x, float y)
        {
            for(auto drawable : Sys::BoxDrawables)
            {
                SDL_SetRenderDrawColor( Sys::Renderer, 255, 255, 255, 255 );
                SDL_RenderFillRect( Sys::Renderer, drawable->getShape(x, y) );
            };
            return false;
        }
    }
    float view_x, view_y;
    bool RenderThings()
    {
        // Clear screen
        // Cheap clear; use SDL_RenderClear() instead of SDL_RenderFillRect() if there are problems
        SDL_SetRenderDrawColor( Renderer, 0, 0, 0, 255);
        SDL_RenderFillRect( Renderer, &shape );
        
        // Come up with center
        for(auto c : Characters.List)
        {
            if(c->myself)
            {
                view_x = floor(c->center_x())-Sys::shape.w/2;
                view_y = floor(c->center_y())-Sys::shape.h/2;
            }
        }
        // Draw simple textured drawables
        Renderers::BoxDrawables(view_x, view_y);
        Renderers::TexturedDrawables(view_x, view_y);
        
        return false;
    }
    bool PresentScreen()
    {
        SDL_RenderPresent(Renderer);
        return false;
    }
}

namespace Maps
{
    long width;
    long height;
    void load_wallmask(const char * filename)
    {
        SDL_Surface * wallmask = IMG_Load(filename);
        int bpp = wallmask->format->BytesPerPixel;
        unsigned char r;
        unsigned char g;
        unsigned char b;
        //std::cout << "Loading wallmask from " << filename << "\n";
        int scale = 4;
        width  = wallmask->w * scale;
        height = wallmask->h * scale;
        
        for (long y = 0; y < wallmask->h; ++y)
        {
            std::cout << "Row " << y << "\n";
            long rect_x = -1;
            for (long x = 0; x < wallmask->w; ++x)
            {
                //std::cout << "Column " << x << "\n";
                SDL_GetRGB(*((unsigned char*)(wallmask->pixels)+(x + y*wallmask->w)*bpp), wallmask->format, &r, &g, &b);
                if(r < 127)
                {
                    if(rect_x == -1) // new rect
                    {
                        rect_x = x;
                    }
                }
                else
                {
                    if(rect_x != -1) // end of rect
                    {
                        auto chunk = new Sys::BoxDrawable(Ent::New());
                        chunk->position->x = scale*rect_x;
                        chunk->hull    ->w = scale*(x-rect_x);
                        chunk->position->y = scale*y;
                        chunk->hull    ->h = scale;
                        rect_x = -1;
                    }
                }
            }
            if(rect_x != -1) // end of rect
            {
                auto chunk = new Sys::BoxDrawable(Ent::New());
                chunk->position->x = scale*rect_x;
                chunk->hull    ->w = scale*(wallmask->w-rect_x);
                chunk->position->y = scale*y;
                chunk->hull    ->h = scale;
            }
        }
    }
}

bool sys_init()
{
    Maps::load_wallmask("wallmask.png");
    
    auto me = new Sys::Character(Ent::New());
    me->myself = true;
    me->center_on(Maps::width/2, Maps::height/2);
    
    Sys::tems.push_back(&Sys::FrameLimit);
    Sys::tems.push_back(&Sys::SDLEvents);
    Sys::tems.push_back(&Sys::Physics);
    Sys::tems.push_back(&Sys::RenderThings);
    Sys::tems.push_back(&Sys::PresentScreen);
    
    return 1;
}

bool main_init()
{
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
        std::cout << "Could not initialize SDL: " << SDL_GetError() << std::endl;

    int MAX_X = 800, MAX_Y = 600;
    
    Sys::view_x = 0;
    Sys::view_y = 0;

    Sys::MainWindow = SDL_CreateWindow("Benetnasch", 300, 300, MAX_X, MAX_Y, SDL_WINDOW_SHOWN);
    if (Sys::MainWindow == nullptr)
        std::cout << "Could not create an SDL window: " << SDL_GetError() << std::endl;
    Sys::Renderer = SDL_CreateRenderer(Sys::MainWindow, -1, SDL_RENDERER_TARGETTEXTURE);
    if (Sys::Renderer == nullptr)
        std::cout << "Could not create an SDL renderer: " << SDL_GetError() << std::endl;
    
    srand(time(NULL));
    
    SDL_PumpEvents();
    Input::Init();
    
    Sys::tems.push_back(&sys_init);
    
    return 0;
}
#ifdef TESTS
int main(int argc, char *argv[])
{
    new Sys::Character(Ent::New());
    /* Character
     * x: 0
     * y: 0
     * w: 32
     * h: 48
     */
    new Sys::BoxDrawable(Ent::New());
    /* BoxDrawable
     * x: 40
     * y: 0
     * w: 10
     * h: 48
     */
    auto d = Sys::BoxDrawables.List[0];
    d->position->x = 40;
    d->position->y = 0;
    d->hull->w = 10;
    d->hull->h = 48;
    
    std::cout << "Testing move_contact right...\n";
    
    Sys::Physicsers::move_contact(Sys::Characters.List[0], 100, 0);
    
    if(8.0f - Sys::Characters.List[0]->position->x < 0.0001)
        std::cout << "move_contact rightwards succeeded\n";
    else
        std::cout << "TEST FAILED move_contact right\n";
    
    
    new Sys::BoxDrawable(Ent::New());
    /* BoxDrawable
     * x: 0
     * y: 64
     * w: 40
     * h: 4
     */
    d = Sys::BoxDrawables.List[1];
    d->position->x = 0;
    d->position->y = 64;
    d->hull->w = 40;
    d->hull->h = 4;
    
    std::cout << "Testing move_contact straight down along a wall...\n";
    
    Sys::Physicsers::move_contact(Sys::Characters.List[0], 0, 100);
    
    if(16.0f - Sys::Characters.List[0]->position->y < 0.0001)
        std::cout << "move_contact straight down along a wall succeeded\n";
    else
        std::cout << "TEST FAILED move_contact straight down along a wall\n";
}
#else // not TESTS
int main(int argc, char *argv[])
{
    if (main_init())
        return 1;
    
    while ( !Sys::quit )
    {
        std::vector<bool(*)()> marked_for_removal;
        for(auto function : Sys::tems)
        {
            auto r = function();
            if(r)
                marked_for_removal.push_back(function);
        }
        for(auto function : marked_for_removal)
        {
            unsigned i;
            for (i = 0; Sys::tems[i] != function and i < Sys::tems.size(); ++i) ;
            if(i == Sys::tems.size())
                std::cout << "Bad system removal in mainloop!";
            else
                Sys::tems.erase(Sys::tems.begin()+i);
        }
    }
    return 0;
}
#endif // TESTS
