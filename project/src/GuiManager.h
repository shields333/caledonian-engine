#pragma once

#include <vk_types.h>

class Fence;
class CommandPool;
class CommandBuffer;
class Image;
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

	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

	void processEvents(SDL_Event &e);
	void newFrame();
	void draw(CommandBuffer &cmd, Image &target);

private:
	std::unique_ptr<CommandPool> m_cmdPool;
	std::unique_ptr<CommandBuffer> m_cmdBuf;

	VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
};