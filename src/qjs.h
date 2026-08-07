#include <memory>

#include "cata_imgui.h"

struct JSContext;
struct JSRuntime;

namespace qjs
{

class Context;

class Runtime
{
        Runtime( JSRuntime *r );
    public:
        ~Runtime();

        static std::shared_ptr<Runtime> make();

        JSRuntime *get() {
            return runtime;
        }

    private:
        JSRuntime *runtime;
};

class Context
{
        Context( JSContext *c, std::shared_ptr<Runtime> r );
    public:
        ~Context();
        static std::shared_ptr<Context> make( std::shared_ptr<Runtime> r );

        JSContext *get() {
            return context;
        }

        void do_stuff();

    private:
        JSContext *context;
        std::shared_ptr<Runtime> runtime;
};

class Console : cataimgui::window
{
    public:
        Console( ) : cataimgui::window( "qjs console" ), ctx{  } {};
        void init();
        void run();
        void draw_controls() override;
        cataimgui::bounds get_bounds() override;
        void on_resized() override {
            init();
        }
    private:
        std::shared_ptr<Context> ctx;
};

}
