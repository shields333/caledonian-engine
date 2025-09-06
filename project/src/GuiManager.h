#pragma once

#include <vk_types.h>

class Image;
class CommandBuffer;
union SDL_Event;

class GuiManager
{
public:
	GuiManager();
	~GuiManager();

	GuiManager(const GuiManager& other) = delete;
	GuiManager operator=(const GuiManager& other) = delete;

	GuiManager(GuiManager&& other) noexcept;
	GuiManager* operator=(GuiManager&& other) noexcept;

	void processEvents(SDL_Event &e);
	void newFrame();
	void draw(CommandBuffer &cmd);

private:

	VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
};