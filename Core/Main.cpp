#include "Renderer.h"

int main(int argc, char* argv[])
{
	Renderer renderer{};

	renderer.Init();
	renderer.Run();
	renderer.Shutdown();

	return 0;
}
