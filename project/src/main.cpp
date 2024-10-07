#include <vk_engine.h>

int main(int argc, char* argv[])
{
	VulkanEngine& engine = VulkanEngine::get();
	
	engine.init();

	engine.run();	

	return 0;
}
