#include <prayground/prayground.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

using namespace std;

/**
 * @todo Optimize bitmap rendering 
 */ 

class App : public BaseApp
{
public:
    void setup()
    {
        const int32_t width = pgGetWidth();
        const int32_t height = pgGetHeight();
        bitmaps.emplace_back("resources/image/"
    }

    void update()
    {
        
    }

    void draw()
    {

    }
private:
    vector<Bitmap> bitmaps;
};

int main()
{
    pgSetAppDir(APP_DIR);

    auto app = make_shared<App>();
    auto window = make_shared<Window>("Bitmap test", 1080, 1080);

    pgRunApp(app, window);
}