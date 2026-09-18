#include "PCH.h"

#include "Preview.h"

#include "PreviewMenu.h"
#include "SKSEMenuFramework.h"
#include "Settings.h"

#include "utils/Logger.h"
#include "utils/Strings.h"

#include <d3d11_1.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>

namespace preview
{
	namespace
	{
		// The texture the capture lands in. 2048 square covers a 4K pane with room to spare; the
		// page crops it with UVs, so nothing is stretched.
		constexpr unsigned int kTexSize = 2048;
		// The capture rectangle is this much larger than the pane, so a model that leans out of
		// its bound is not clipped at the edge (Modex's safety margin).
		constexpr float kSafetyMargin = 1.5F;
		// A page that has not drawn the pane for this long has gone away: close the helper menu.
		constexpr double kHeartbeatMs = 400.0;

		std::mutex g_lock;   // the request and the heartbeat, written by the page's thread
		RE::TESBoundObject* g_requested = nullptr;
		float g_paneW = 0.0F, g_paneH = 0.0F;
		std::chrono::steady_clock::time_point g_heartbeat{};
		bool g_heartbeatValid = false;

		// Main-thread state (the helper menu's).
		bool g_running = false;
		bool g_texturesReady = false;
		RE::TESBoundObject* g_current = nullptr;
		std::atomic<std::uint32_t> g_loads{ 0 }, g_renders{ 0 };
		std::string g_lastError;
		std::atomic<bool> g_openQueued{ false };

		// The engine's own switch for inventory 3D. Inventory3DManager::LoadInventoryItem's first
		// instruction is a read of this Setting's value and it returns at once when it is off
		// (disassembled 2026-09-18: id 50885 +0x5F, `cmp byte [id 510027], 0; je exit`). A player
		// whose INI turns it off - it was off on the owner's own machine for the whole first attempt
		// (logic library entry 54) - would see an empty pane and nothing to explain it, so the
		// switch is turned on while the helper menu is open and put back when it closes.
		constexpr const char* kInventory3DSetting = "bShowInventory3D:Interface";
		bool g_settingForced = false;

		// The inventory's own render, as InventoryMenu::PostDisplay tail-calls it on SE 1.5.97: Address Library
		// id 50882 (0x887750), a plain (this) call that finishes a pending load task, then draws loadedModels
		// through the UI 3D scene render (id 51855). It is called by id on SE because CommonLibSSE-NG's
		// Inventory3DManager::Render binding was found pointing at a three-argument sibling on this runtime
		// (0x888490: this, a camera pointer and a zoom float), which with one argument places nothing.
		void EngineRender(RE::Inventory3DManager* a_mgr)
		{
			if (REL::Module::IsSE())
			{
				static REL::Relocation<void(RE::Inventory3DManager*)> render{ REL::ID(50882) };
				render(a_mgr);
				return;
			}
			a_mgr->Render();
		}

		RE::Setting* Inventory3DSetting()
		{
			auto* ini = RE::INISettingCollection::GetSingleton();
			return ini ? ini->GetSetting(kInventory3DSetting) : nullptr;
		}

		ID3D11Texture2D*          g_dstTex = nullptr;
		ID3D11ShaderResourceView* g_dstSRV = nullptr;
		ID3D11Texture2D*          g_scratchTex = nullptr;

		// What the last capture produced, read by the page.
		std::atomic<float> g_capturedW{ 0.0F }, g_capturedH{ 0.0F };
		std::atomic<float> g_modelInTexX{ 0.0F }, g_modelInTexY{ 0.0F };
		std::atomic<float> g_innerW{ 0.0F }, g_innerH{ 0.0F };
		std::atomic<bool> g_hasImage{ false };

		// Diagnostics (read by GetStatus).
		std::atomic<bool> g_noRestore{ false };
		std::atomic<int> g_rectL{ 0 }, g_rectT{ 0 }, g_rectW{ 0 }, g_rectH{ 0 };
		std::atomic<float> g_boundX{ 0 }, g_boundY{ 0 }, g_boundZ{ 0 }, g_radius{ 0 }, g_transX{ 0 }, g_transY{ 0 }, g_transZ{ 0 }, g_screenX{ 0 }, g_screenY{ 0 };
		std::atomic<std::uint32_t> g_screenW{ 0 }, g_screenH{ 0 };
		std::atomic<std::uint32_t> g_targetW{ 0 }, g_targetH{ 0 };
		std::atomic<int> g_targetFormat{ -1 };


		template <class T>
		void Release(T*& a_p)
		{
			if (a_p) { a_p->Release(); a_p = nullptr; }
		}

		void ReleaseTextures()
		{
			Release(g_dstSRV);
			Release(g_dstTex);
			Release(g_scratchTex);
			g_texturesReady = false;
			g_hasImage = false;
		}

		// The textures are made in the back buffer's own format so CopySubresourceRegion is a plain
		// copy (the format must match or the copy is silently refused).
		bool CreateTextures()
		{
			if (g_texturesReady) { return true; }
			auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
			if (!renderer) { g_lastError = "no renderer"; return false; }
			auto& data = renderer->GetRuntimeData();
			auto* device = reinterpret_cast<ID3D11Device*>(data.forwarder);
			auto* context = reinterpret_cast<ID3D11DeviceContext*>(data.context);
			if (!device || !context) { g_lastError = "no device or context"; return false; }
			// The target the engine has bound right now - the one its Render() will paint into.
			ID3D11RenderTargetView* rtv = nullptr;
			context->OMGetRenderTargets(1, &rtv, nullptr);
			if (!rtv) { g_lastError = "no render target bound"; return false; }

			ID3D11Resource* srcRes = nullptr;
			rtv->GetResource(&srcRes);
			rtv->Release();
			if (!srcRes) { g_lastError = "render view has no resource"; return false; }
			ID3D11Texture2D* srcTex = nullptr;
			srcRes->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&srcTex));
			srcRes->Release();
			if (!srcTex) { g_lastError = "back buffer is not a 2D texture"; return false; }
			D3D11_TEXTURE2D_DESC srcDesc{};
			srcTex->GetDesc(&srcDesc);
			srcTex->Release();

			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = kTexSize;
			desc.Height = kTexSize;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = srcDesc.Format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(device->CreateTexture2D(&desc, nullptr, &g_dstTex))) { g_lastError = "CreateTexture2D (destination) failed"; ReleaseTextures(); return false; }
			if (FAILED(device->CreateShaderResourceView(g_dstTex, nullptr, &g_dstSRV))) { g_lastError = "CreateShaderResourceView failed"; ReleaseTextures(); return false; }
			if (FAILED(device->CreateTexture2D(&desc, nullptr, &g_scratchTex))) { g_lastError = "CreateTexture2D (scratch) failed"; ReleaseTextures(); return false; }
			g_texturesReady = true;
			logger::info("preview: capture textures created ({}x{}, format {})", kTexSize, kTexSize, static_cast<int>(srcDesc.Format));
			return true;
		}
	}

	bool Available()
	{
		return RE::Inventory3DManager::GetSingleton() != nullptr && RE::BSGraphics::Renderer::GetSingleton() != nullptr;
	}

	void Heartbeat()
	{
		{
			std::scoped_lock l(g_lock);
			g_heartbeat = std::chrono::steady_clock::now();
			g_heartbeatValid = true;
		}
		if (!settings::general::show3DPreview || !Available()) { return; }
		if (PreviewMenuOpen() || g_openQueued.load()) { return; }
		// The UI message queue belongs to the main thread; this is the framework's Present hook.
		g_openQueued = true;
		if (auto* tasks = SKSE::GetTaskInterface())
		{
			tasks->AddTask([]() { SetPreviewMenuOpen(true); g_openQueued = false; });
		}
		else { g_openQueued = false; }
	}

	void Request(RE::TESForm* a_form, float a_paneWidth, float a_paneHeight)
	{
		auto* bound = a_form ? a_form->As<RE::TESBoundObject>() : nullptr;
		std::scoped_lock l(g_lock);
		g_requested = bound;
		g_paneW = a_paneWidth;
		g_paneH = a_paneHeight;
	}

	void* TextureID() { return g_hasImage.load() ? static_cast<void*>(g_dstSRV) : nullptr; }
	bool HasImage() { return g_hasImage.load() && g_dstSRV != nullptr; }

	void DisplayUV(float& a_u0, float& a_v0, float& a_u1, float& a_v1)
	{
		const float tex = static_cast<float>(kTexSize);
		const float capW = g_capturedW.load(), capH = g_capturedH.load();
		const float innerW = g_innerW.load(), innerH = g_innerH.load();
		const float w = innerW > 0.0F ? (std::min)(innerW, capW) : capW;
		const float h = innerH > 0.0F ? (std::min)(innerH, capH) : capH;
		const float x0 = g_modelInTexX.load() - w * 0.5F;
		const float y0 = g_modelInTexY.load() - h * 0.5F;
		a_u0 = x0 / tex; a_v0 = y0 / tex;
		a_u1 = (x0 + w) / tex; a_v1 = (y0 + h) / tex;
	}

	bool PaneRect(float& a_x0, float& a_y0, float& a_x1, float& a_y1)
	{
		ImGuiMCP::ImGuiIO* io = ImGuiMCP::GetIO();
		if (!io || io->DisplaySize.x <= 0.0F || io->DisplaySize.y <= 0.0F) { return false; }
		const float side = settings::preview::paneSize * io->DisplaySize.y;
		const float cx = settings::preview::paneX * io->DisplaySize.x;
		const float cy = settings::preview::paneY * io->DisplaySize.y;
		a_x0 = cx - side * 0.5F; a_y0 = cy - side * 0.5F;
		a_x1 = cx + side * 0.5F; a_y1 = cy + side * 0.5F;
		return true;
	}

	void DrawFloating(const char* a_title)
	{
		if (!settings::general::show3DPreview) { return; }
		float x0 = 0.0F, y0 = 0.0F, x1 = 0.0F, y1 = 0.0F;
		if (!PaneRect(x0, y0, x1, y1)) { return; }
		ImGuiMCP::ImDrawList* dl = ImGuiMCP::GetForegroundDrawList();
		if (!dl) { return; }

		auto col = [](int r, int g, int b, int a) { return static_cast<ImGuiMCP::ImU32>((a << 24) | (b << 16) | (g << 8) | r); };
		// Black behind, a white frame around, the model opaque in front, nothing faded (the owner,
		// 2026-09-18).
		const int alpha = static_cast<int>(std::clamp(settings::preview::paneAlpha, 0.1F, 1.0F) * 255.0F);
		const auto bg = col(static_cast<int>(settings::preview::backgroundR * 255.0F), static_cast<int>(settings::preview::backgroundG * 255.0F),
							static_cast<int>(settings::preview::backgroundB * 255.0F), alpha);
		const ImGuiMCP::ImU32 kLine = col(255, 255, 255, 230);
		const ImGuiMCP::ImU32 kText = col(255, 255, 255, 240);
		const ImGuiMCP::ImU32 kDim = col(255, 255, 255, 150);

		// The faded background first, then the model OPAQUE on top of it: the capture is cleared to
		// transparent, so only the model's pixels land here (the owner, 2026-09-18: the model must
		// not look hazy).
		ImGuiMCP::ImDrawListManager::AddRectFilled(dl, ImGuiMCP::ImVec2{ x0, y0 }, ImGuiMCP::ImVec2{ x1, y1 }, bg, 0.0F, 0);
		if (HasImage())
		{
			float u0 = 0.0F, v0 = 0.0F, u1 = 1.0F, v1 = 1.0F;
			DisplayUV(u0, v0, u1, v1);
			ImGuiMCP::ImDrawListManager::AddImage(dl, TextureID(), ImGuiMCP::ImVec2{ x0, y0 }, ImGuiMCP::ImVec2{ x1, y1 },
												  ImGuiMCP::ImVec2{ u0, v0 }, ImGuiMCP::ImVec2{ u1, v1 }, col(255, 255, 255, 255));
		}

		if (!settings::preview::showFrame) { return; }
		// A white frame all the way round.
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y0 }, ImGuiMCP::ImVec2{ x1, y0 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y0 }, ImGuiMCP::ImVec2{ x1, y1 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y1 }, ImGuiMCP::ImVec2{ x0, y1 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y1 }, ImGuiMCP::ImVec2{ x0, y0 }, kLine, 2.0F);

		// The caption sits INSIDE the box, along its top edge (the owner, 2026-09-18: it was
		// spilling outside the bounds): the item's name, or what to do.
		const char* caption = nullptr;
		ImGuiMCP::ImU32 captionCol = kText;
		if (!Available()) { caption = strings::TR("AIE_PreviewUnavailable", "The 3D preview is not available on this runtime."); captionCol = kDim; }
		else if (a_title && a_title[0]) { caption = a_title; }
		else { caption = strings::TR("AIE_PreviewPickARow", "Point at an item to see it here."); captionCol = kDim; }
		ImGuiMCP::ImDrawListManager::AddText(dl, ImGuiMCP::ImVec2{ x0 + 10.0F, y0 + 8.0F }, captionCol, caption, nullptr);
	}

	bool MenuShouldClose()
	{
		std::scoped_lock l(g_lock);
		if (!g_heartbeatValid) { return true; }
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_heartbeat).count();
		return ms > kHeartbeatMs || !settings::general::show3DPreview;
	}

	void OnMenuShown()
	{
		if (g_running) { return; }
		if (auto* setting = Inventory3DSetting())
		{
			if (!setting->GetBool())
			{
				setting->data.b = true;
				g_settingForced = true;
				logger::info("preview: {} was off in the game's INI; on while the preview is open", kInventory3DSetting);
			}
		}
		else { logger::warn("preview: the game has no {} setting; the engine may refuse to build the model", kInventory3DSetting); }
		if (auto* mgr = RE::Inventory3DManager::GetSingleton())
		{
			mgr->Begin3D(RE::INTERFACE_LIGHT_SCHEME::kInventory);
			g_running = true;
			logger::info("preview: Begin3D(kInventory)");
		}
	}

	void OnMenuHidden()
	{
		if (auto* mgr = RE::Inventory3DManager::GetSingleton(); mgr && g_running)
		{
			mgr->UnloadInventoryItem();
			mgr->End3D();
		}
		g_running = false;
		g_current = nullptr;
		ReleaseTextures();
		if (g_settingForced)
		{
			if (auto* setting = Inventory3DSetting()) { setting->data.b = false; }
			g_settingForced = false;
		}
		logger::info("preview: End3D, model released");
	}

	void RenderFromMenu()
	{
		if (!g_running) { return; }
		RE::TESBoundObject* want = nullptr;
		float paneW = 0.0F, paneH = 0.0F;
		{
			std::scoped_lock l(g_lock);
			want = g_requested;
			paneW = g_paneW; paneH = g_paneH;
		}
		auto* inv = RE::Inventory3DManager::GetSingleton();
		if (!inv) { return; }

		if (want != g_current)
		{
			inv->UnloadInventoryItem();
			g_hasImage = false;
			if (want) { inv->LoadInventoryItem(want, nullptr); ++g_loads; }
			g_current = want;
		}
		if (!g_current || paneW <= 0.0F || paneH <= 0.0F) { return; }
		if (!g_texturesReady && !CreateTextures()) { return; }

		// The rectangle: the pane, grown by the model scale (a smaller scale means capturing more
		// of the screen and shrinking it into the pane) and by the safety margin.
		const float scale = settings::preview::modelScale > 0.0F ? settings::preview::modelScale : 1.0F;
		const float expand = 1.0F / scale;
		const float innerW = paneW * expand, innerH = paneH * expand;
		const float captureW = innerW * kSafetyMargin, captureH = innerH * kSafetyMargin;

		// Where the engine is about to paint the model: its bound centre projected through the UI
		// scene's frustum (Modex issue #48 - the projection the inventory itself uses).
		float capX = 0.0F, capY = 0.0F;
		bool centred = false;
		{
			auto* scn = RE::UI3DSceneManager::GetSingleton();
			auto& rt = inv->GetRuntimeData();
			if (scn && !rt.loadedModels.empty())
			{
				auto* model = rt.loadedModels.back().spModel.get();
				if (model && model->worldBound.radius > 0.0F)
				{
					const auto& vf = scn->viewFrustum;
					const auto& t = model->local.translate;
					const float worldMinX = -vf.fLeft * t.y;
					const float worldMinZ = -vf.fBottom * t.y;
					const float worldW = -vf.fRight * t.y - worldMinX;
					const float worldH = -vf.fTop * t.y - worldMinZ;
					const auto sz = RE::BSGraphics::Renderer::GetScreenSize();
					if (sz.width > 0 && sz.height > 0)
					{
						const float ratioX = worldW / static_cast<float>(sz.width);
						const float ratioY = worldH / static_cast<float>(sz.height);
						if (ratioX != 0.0F && ratioY != 0.0F)
						{
						const auto& c = model->worldBound.center;
							const float modelSX = -(c.x + worldMinX) / ratioX;
							const float modelSY = -(c.z + worldMinZ) / ratioY;
							capX = modelSX - captureW * 0.5F;
							capY = modelSY - captureH * 0.5F;
							centred = true;
							g_boundX = c.x; g_boundY = c.y; g_boundZ = c.z; g_radius = model->worldBound.radius;
							g_transX = t.x; g_transY = t.y; g_transZ = t.z;
							g_screenX = modelSX; g_screenY = modelSY;
							g_screenW = sz.width; g_screenH = sz.height;
						}
					}
				}
			}
		}
		if (!centred)
		{
			// The model has not been built yet: the engine finishes the load inside Render() itself, so
			// Render is called bare - nothing is on screen yet to capture - and the capture waits a frame.
			EngineRender(inv);
			return;
		}

		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (!renderer) { return; }
		auto& data = renderer->GetRuntimeData();
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(data.context);
		if (!context || !g_dstTex || !g_scratchTex) { return; }
		// The bound target: where the engine's Render() is about to paint.
		ID3D11RenderTargetView* rtv = nullptr;
		context->OMGetRenderTargets(1, &rtv, nullptr);
		if (!rtv) { g_lastError = "no render target bound at PostDisplay"; return; }

		ID3D11Resource* srcRes = nullptr;
		rtv->GetResource(&srcRes);
		if (!srcRes) { rtv->Release(); return; }
		ID3D11Texture2D* srcTex = nullptr;
		srcRes->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&srcTex));
		srcRes->Release();
		if (!srcTex) { rtv->Release(); return; }
		D3D11_TEXTURE2D_DESC srcDesc{};
		srcTex->GetDesc(&srcDesc);
		g_targetW = srcDesc.Width; g_targetH = srcDesc.Height; g_targetFormat = static_cast<int>(srcDesc.Format);

		const auto screen = RE::BSGraphics::Renderer::GetScreenSize();
		int left = static_cast<int>(capX), top = static_cast<int>(capY);
		int width = static_cast<int>(captureW), height = static_cast<int>(captureH);
		if (left < 0) { width += left; left = 0; }
		if (top < 0) { height += top; top = 0; }
		width = (std::min)(width, static_cast<int>(screen.width) - left);
		height = (std::min)(height, static_cast<int>(screen.height) - top);
		width = (std::min)(width, static_cast<int>(kTexSize));
		height = (std::min)(height, static_cast<int>(kTexSize));
		if (width <= 0 || height <= 0) { srcTex->Release(); rtv->Release(); return; }

		D3D11_BOX box{};
		box.left = static_cast<UINT>(left); box.top = static_cast<UINT>(top); box.front = 0;
		box.right = static_cast<UINT>(left + width); box.bottom = static_cast<UINT>(top + height); box.back = 1;

		// 1. save the world pixels under the rectangle
		context->CopySubresourceRegion(g_scratchTex, 0, 0, 0, 0, srcTex, 0, &box);
		// 2. flat background, so the capture is model-on-colour rather than model-on-world
		{
			ID3D11DeviceContext1* ctx1 = nullptr;
			if (SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1))) && ctx1)
			{
				const D3D11_RECT rect{ left, top, left + width, top + height };
				// Cleared to TRANSPARENT: the capture then carries only the model's own pixels (alpha = its
				// coverage), so the page can lay it opaque over a faded background instead of fading both.
				const FLOAT bg[4] = { settings::preview::backgroundR, settings::preview::backgroundG, settings::preview::backgroundB, 0.0F };
				ctx1->ClearView(rtv, bg, &rect, 1);
				ctx1->Release();
			}
		}
		// 3. the engine paints the model
		EngineRender(inv);
		// 4. lift the rectangle into our texture (top-left; the page crops with UVs)
		context->CopySubresourceRegion(g_dstTex, 0, 0, 0, 0, srcTex, 0, &box);
		// 5. put the world pixels back
		if (!g_noRestore.load())
		{
			D3D11_BOX sbox{};
			sbox.left = 0; sbox.top = 0; sbox.front = 0;
			sbox.right = static_cast<UINT>(width); sbox.bottom = static_cast<UINT>(height); sbox.back = 1;
			context->CopySubresourceRegion(srcTex, 0, static_cast<UINT>(left), static_cast<UINT>(top), 0, g_scratchTex, 0, &sbox);
		}
		srcTex->Release();
		rtv->Release();
		g_rectL = left; g_rectT = top; g_rectW = width; g_rectH = height;

		g_capturedW = static_cast<float>(width);
		g_capturedH = static_cast<float>(height);
		g_innerW = innerW;
		g_innerH = innerH;
		const float modelCX = capX + captureW * 0.5F, modelCY = capY + captureH * 0.5F;
		g_modelInTexX = modelCX - static_cast<float>(left) - settings::preview::offsetX;
		g_modelInTexY = modelCY - static_cast<float>(top) - settings::preview::offsetY;
		g_hasImage = true;
		++g_renders;
	}

	Status GetStatus()
	{
		Status s;
		s.available = Available();
		s.menuOpen = PreviewMenuOpen();
		s.running = g_running;
		s.textures = g_texturesReady;
		s.currentFormID = g_current ? g_current->GetFormID() : 0;
		{
			std::scoped_lock l(g_lock);
			s.requestedFormID = g_requested ? g_requested->GetFormID() : 0;
			s.paneW = g_paneW; s.paneH = g_paneH;
			s.sinceHeartbeatMs = g_heartbeatValid ? std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_heartbeat).count() : -1.0;
		}
		s.loads = g_loads.load();
		s.renders = g_renders.load();
		if (auto* inv = RE::Inventory3DManager::GetSingleton()) { s.managerModels = static_cast<std::uint32_t>(inv->GetRuntimeData().loadedModels.size()); }
		s.capturedW = g_capturedW.load(); s.capturedH = g_capturedH.load();
		s.lastError = g_lastError;
		if (auto* setting = Inventory3DSetting()) { s.inventory3DSetting = setting->GetBool() ? 1 : 0; }
		s.settingForced = g_settingForced;
		s.rectL = g_rectL.load(); s.rectT = g_rectT.load(); s.rectW = g_rectW.load(); s.rectH = g_rectH.load();
		s.boundX = g_boundX.load(); s.boundY = g_boundY.load(); s.boundZ = g_boundZ.load(); s.radius = g_radius.load();
		s.transX = g_transX.load(); s.transY = g_transY.load(); s.transZ = g_transZ.load();
		s.screenX = g_screenX.load(); s.screenY = g_screenY.load(); s.screenW = g_screenW.load(); s.screenH = g_screenH.load();
		s.noRestore = g_noRestore.load();
		s.targetW = g_targetW.load(); s.targetH = g_targetH.load(); s.targetFormat = g_targetFormat.load();
		return s;
	}

	void SetNoRestore(bool a_on) { g_noRestore = a_on; }
}
