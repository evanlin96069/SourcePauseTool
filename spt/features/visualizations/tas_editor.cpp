#include "stdafx.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <functional>

#include "spt/feature.hpp"
#include "spt/utils/file.hpp"
#include "spt/features/tas.hpp"
#include "spt/scripts/srctas_reader.hpp"
#include "imgui/imgui_interface.hpp"
#include "imgui/ImGuiColorTextEdit/TextEditor.h"
#include "imgui/ImGuiFileDialog/ImGuiFileDialog.h"

#undef min
#undef max

#define OPEN_DIALOG_KEY "TASEditorOpenDlg"
#define SAVE_DIALOG_KEY "TASEditorSaveDlg"
#define NEW_DIALOG_KEY "TASEditorNewDlg"

#define NEW_POPUP_ID "New Script##popup"
#define UNSAVED_POPUP_ID "Unsaved Changes##popup"
#define OVERWRITE_POPUP_ID "Overwirte File##popup"

#define BACKUP_FILE_NAME "spt_tas_editor_backup.txt"

// TAS script editor
class TASEditor : public FeatureWrapper<TASEditor>
{
protected:
	virtual void LoadFeature()
	{
		igfd = std::make_unique<ImGuiFileDialog>();

		// TODO make these colors global somewhere?
		static ImVec4 fileColor = ImVec4(0.2f, 1.0f, .9f, 1.f);
		igfd->SetFileStyle(IGFD_FileStyleByTypeDir, "", ImVec4(1.f, 1.f, 1.f, 1.f), ICON_CI_FOLDER);
		igfd->SetFileStyle(IGFD_FileStyleByTypeFile, "", fileColor, ICON_CI_FILE);

		SptImGuiGroup::TASEditor.RegisterUserCallback([this]() { this->ImGuiCallback(); });
		LoadBackup();
	}

	virtual void UnloadFeature()
	{
		SaveBackup();
		igfd.reset();
	}

private:
	struct EditorState
	{
		std::filesystem::path currentFilePath;
		std::filesystem::file_time_type openedFileTime;
		bool unsavedChanges = false;
		bool hasFileOpen = false;
	};

	// Editor
	TextEditor editor;
	EditorState state;

	// ImGui utilities
	std::unique_ptr<ImGuiFileDialog> igfd;
	SptImGui::TimedToolTip errorTooltip;
	std::string errorStr;

	// Pending actions
	std::function<void()> pendingAction;   // Action deferred to the next frame
	bool pendingUnsavedPopup = false;      // True while the unsaved-changes popup is waiting for user input
	std::filesystem::path pendingSavePath; // Used for overwrite confirmation
	std::function<void()> onSaveCallback;  // Callback to run after a successful save from CheckModifiedAndSave
	char pendingSaveName[MAX_PATH] = "";   // Used for the "New Script" popup

	// Playback
	int playbackTicks = 0;
	bool autoScrollToLine = true;
	int lastExecutingLine = 0;

	std::string GetDisplayFileName()
	{
		if (!state.hasFileOpen)
			return "";
		if (state.currentFilePath.empty())
			return "Untitled";
		return state.currentFilePath.filename().string();
	}

	std::filesystem::path GetBackupFilePath()
	{
		return std::filesystem::path(GetGameDir()) / BACKUP_FILE_NAME;
	}

	void SaveBackup()
	{
		auto backupPath = GetBackupFilePath();
		if (!state.hasFileOpen || !state.unsavedChanges)
			return;

		std::ofstream file(backupPath);
		if (!file.is_open())
			return;

		file << state.currentFilePath.string() << '\n';
		file << editor.GetText();
	}

	void LoadBackup()
	{
		auto backupPath = GetBackupFilePath();
		if (!std::filesystem::exists(backupPath))
			return;

		std::ifstream file(backupPath);
		if (!file.is_open())
			return;

		std::string filePath;
		std::getline(file, filePath);

		std::stringstream ss;
		ss << file.rdbuf();
		file.close();

		editor.SetText(ss.str());
		state.hasFileOpen = true;
		state.unsavedChanges = true;
		state.currentFilePath = filePath;
		state.openedFileTime = std::filesystem::file_time_type::min();

		std::error_code ec;
		std::filesystem::remove(backupPath, ec);
	}

	void LoadContent(const std::string& content)
	{
		editor.SetText(content);
		state.currentFilePath.clear();
		state.unsavedChanges = true;
		state.hasFileOpen = true;
		state.openedFileTime = std::filesystem::file_time_type::min();
	}

	bool LoadFile(const std::filesystem::path& filePath)
	{
		std::ifstream file(filePath);
		if (!file.is_open())
			return false;

		std::stringstream ss;
		ss << file.rdbuf();
		editor.SetText(ss.str());
		state.hasFileOpen = true;
		state.unsavedChanges = false;
		state.currentFilePath = filePath;
		state.openedFileTime = std::filesystem::last_write_time(filePath);
		return true;
	}

	bool SaveFile(const std::filesystem::path& filePath)
	{
		std::ofstream file(filePath);
		if (!file.is_open())
			return false;

		std::string text = editor.GetText();

		// Remove the trailing newline if it exists
		// TextEditor::GetText() always adds an extra newline at the end, seems to be a bug.
		if (!text.empty() && text.back() == '\n')
			text.pop_back();

		file << text;
		file.close(); // ensure file time is updated after writing

		state.currentFilePath = filePath;
		state.unsavedChanges = false;
		state.openedFileTime = std::filesystem::last_write_time(filePath);
		return true;
	}

	void OpenNewPopup()
	{
		pendingSaveName[0] = '\0';
		ImGui::OpenPopup(NEW_POPUP_ID);
	}

	void OpenFileDialog()
	{
		IGFD::FileDialogConfig config;
		config.path = state.hasFileOpen ? std::filesystem::path(state.currentFilePath).parent_path().string()
		                                : GetGameDir();
		igfd->OpenDialog(OPEN_DIALOG_KEY, "Open TAS Script", ".srctas", config);
	}

	bool IsFileModifiedOnDisk(const std::filesystem::path& filePath)
	{
		if (filePath.empty())
			return false;
		if (state.openedFileTime == std::filesystem::file_time_type::min())
			return std::filesystem::exists(filePath);

		std::error_code ec;
		auto currentTime = std::filesystem::last_write_time(filePath, ec);
		return !ec && currentTime != state.openedFileTime;
	}

	void CheckModifiedAndSave(const std::filesystem::path& filePath, std::function<void()> callback = nullptr)
	{
		onSaveCallback = std::move(callback);
		if (IsFileModifiedOnDisk(filePath))
		{
			pendingSavePath = filePath;
			ImGui::OpenPopup(OVERWRITE_POPUP_ID);
		}
		else
		{
			if (!SaveFile(filePath))
			{
				errorStr = "Failed to save file: " + filePath.string();
				errorTooltip.StartShowing(errorStr.c_str());
				onSaveCallback = nullptr;
			}
			else if (onSaveCallback)
			{
				auto cb = std::move(onSaveCallback);
				onSaveCallback = nullptr;
				cb();
			}
		}
	}

	bool CheckUnsavedAndProceed(std::function<void()> action)
	{
		if (state.hasFileOpen && state.unsavedChanges)
		{
			pendingAction = std::move(action);
			pendingUnsavedPopup = true;
			ImGui::OpenPopup(UNSAVED_POPUP_ID);
			return false;
		}
		action();
		return true;
	}

	void PlayScript()
	{
		if (!state.hasFileOpen || state.currentFilePath.empty())
			return;

		auto doPlay = [this]()
		{
			std::string path = state.currentFilePath.string();
			if (playbackTicks > 0)
				scripts::g_TASReader.ExecuteScriptWithResume(path, playbackTicks);
			else
				scripts::g_TASReader.ExecuteScript(path);
		};

		if (state.unsavedChanges)
			CheckModifiedAndSave(state.currentFilePath, doPlay);
		else
			doPlay();
	}

	void ImGuiCallback()
	{
		// Track unsaved changes
		if (state.hasFileOpen && editor.IsTextChanged())
			state.unsavedChanges = true;

		// Execute pending action from previous frame
		if (pendingAction && !pendingUnsavedPopup)
		{
			auto action = std::move(pendingAction);
			pendingAction = nullptr;
			action();
		}

		// Toolbar
		if (ImGui::Button(ICON_CI_NEW_FILE " New"))
			CheckUnsavedAndProceed([this]() { OpenNewPopup(); });

		ImGui::SameLine();
		if (ImGui::Button(ICON_CI_FOLDER_OPENED " Open"))
			CheckUnsavedAndProceed([this]() { OpenFileDialog(); });

		if (state.hasFileOpen)
		{
			ImGui::SameLine();
			if (ImGui::Button(ICON_CI_SAVE " Save"))
				CheckModifiedAndSave(state.currentFilePath);

			ImGui::SameLine();
			if (ImGui::Button(ICON_CI_SAVE_AS " Save As"))
			{
				IGFD::FileDialogConfig config;
				config.path = std::filesystem::path(state.currentFilePath).parent_path().string();
				config.fileName = GetDisplayFileName();
				config.flags = ImGuiFileDialogFlags_ConfirmOverwrite;
				igfd->OpenDialog(SAVE_DIALOG_KEY, "Save TAS Script As", ".srctas", config);
			}

			ImGui::NextColumn();
			ImGui::SetNextItemWidth(100);
			ImGui::InputInt("Ticks", &playbackTicks, 0, 0);
			if (playbackTicks < 0)
				playbackTicks = 0;
			ImGui::SameLine();
			SptImGui::HelpMarker(
			    "The script is played back at maximal FPS and without rendering until this many ticks before the end of the script.");
			ImGui::SameLine();
			if (ImGui::Button(ICON_CI_PLAY " Play"))
				PlayScript();

			ImGui::SameLine();
			bool isExecuting = scripts::g_TASReader.IsExecutingScript();
			if (isExecuting)
			{
				int curTick = scripts::g_TASReader.GetCurrentTick();
				int totalTicks = scripts::g_TASReader.GetCurrentScriptLength();
				ImGui::Text("Tick %d / %d", curTick, totalTicks);

				int execLine = scripts::g_TASReader.GetCurrentExecutingLine();
				TextEditor::Breakpoints bps;
				if (execLine > 0)
					bps.insert(execLine);
				editor.SetBreakpoints(bps);

				if (autoScrollToLine && execLine > 0 && execLine != lastExecutingLine)
				{
					editor.SetCursorPosition(TextEditor::Coordinates(execLine - 1, 0));
					lastExecutingLine = execLine;
				}
			}
			else
			{
				ImGui::TextDisabled("Not playing");
				editor.SetBreakpoints({});
				lastExecutingLine = 0;
			}

			ImGui::SameLine();
			ImGui::Checkbox("Auto-scroll", &autoScrollToLine);
			ImGui::SameLine();
			SptImGui::HelpMarker("Automatically scrolls the editor to the currently executing line.");
		}

		// Error tooltip
		errorTooltip.Show(SPT_IMGUI_WARN_COLOR_YELLOW, 2.0);

		// Unsaved changes confirmation popup
		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal(UNSAVED_POPUP_ID, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Do you want to save changes to %s?", GetDisplayFileName().c_str());
			ImGui::Spacing();

			if (ImGui::Button("Save", ImVec2(120, 0)))
			{
				pendingUnsavedPopup = false;
				ImGui::CloseCurrentPopup();
				CheckModifiedAndSave(state.currentFilePath);
				// pendingAction already set, fires next frame
			}
			ImGui::SameLine();
			if (ImGui::Button("Don't Save", ImVec2(120, 0)))
			{
				pendingUnsavedPopup = false;
				ImGui::CloseCurrentPopup();
				// pendingAction already set, fires next frame
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				pendingUnsavedPopup = false;
				pendingAction = nullptr; // discard the pending action
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// New script popup
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal(NEW_POPUP_ID, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter the save name to load in the script:");
			bool enterPressed = ImGui::InputText("##save_name",
			                                     pendingSaveName,
			                                     sizeof(pendingSaveName),
			                                     ImGuiInputTextFlags_EnterReturnsTrue);

			bool canCreate = pendingSaveName[0] != '\0';

			if (!canCreate)
				ImGui::BeginDisabled();
			if (ImGui::Button("Create", ImVec2(120, 0)) || (enterPressed && canCreate))
			{
				// Defer opening the file dialog to the next frame
				pendingAction = [this]()
				{
					IGFD::FileDialogConfig config;
					config.path = GetGameDir();
					config.flags = ImGuiFileDialogFlags_ConfirmOverwrite;
					igfd->OpenDialog(NEW_DIALOG_KEY, "Save New TAS Script", ".srctas", config);
				};
				ImGui::CloseCurrentPopup();
			}
			if (!canCreate)
				ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
				ImGui::CloseCurrentPopup();

			ImGui::EndPopup();
		}

		// File ovewrite confirmation popup
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal(OVERWRITE_POPUP_ID, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (state.openedFileTime == std::filesystem::file_time_type::min())
			{
				ImGui::Text("The file \"%s\" already exists on disk.",
				            pendingSavePath.filename().string().c_str());
			}
			else
			{
				ImGui::Text("The file \"%s\" has been modified on disk since it was opened.",
				            pendingSavePath.filename().string().c_str());
			}
			ImGui::Text("Do you want to overwrite it with your changes?");
			ImGui::Spacing();

			if (ImGui::Button("Overwrite", ImVec2(120, 0)))
			{
				if (!SaveFile(pendingSavePath))
				{
					errorStr = "Failed to save file: " + pendingSavePath.string();
					errorTooltip.StartShowing(errorStr.c_str());
					onSaveCallback = nullptr;
				}
				else if (onSaveCallback)
				{
					auto cb = std::move(onSaveCallback);
					onSaveCallback = nullptr;
					cb();
				}
				pendingSavePath.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				onSaveCallback = nullptr;
				pendingSavePath.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// File dialogs
		ImVec2 minSize(600, 400);
		ImVec2 maxSize(FLT_MAX, FLT_MAX);

		if (igfd->Display(NEW_DIALOG_KEY, ImGuiWindowFlags_NoCollapse, minSize, maxSize))
		{
			if (igfd->IsOk())
			{
				errorTooltip.StopShowing();

				std::string filePath = igfd->GetFilePathName();
				LoadContent(spt_tas.GetTemplateScript(filePath.c_str(), pendingSaveName));
				state.currentFilePath = filePath;
			}
			igfd->Close();
		}

		if (igfd->Display(OPEN_DIALOG_KEY, ImGuiWindowFlags_NoCollapse, minSize, maxSize))
		{
			if (igfd->IsOk())
			{
				errorTooltip.StopShowing();

				std::string filePath = igfd->GetFilePathName();
				if (!LoadFile(filePath))
				{
					errorStr = "Failed to load file: " + filePath;
					errorTooltip.StartShowing(errorStr.c_str());
				}
			}
			igfd->Close();
		}

		if (igfd->Display(SAVE_DIALOG_KEY, ImGuiWindowFlags_NoCollapse, minSize, maxSize))
		{
			if (igfd->IsOk())
			{
				errorTooltip.StopShowing();

				std::string filePath = igfd->GetFilePathName();
				SaveFile(filePath);
			}
			igfd->Close();
		}

		// Editor & status bar
		if (state.hasFileOpen)
		{
			auto cpos = editor.GetCursorPosition();
			ImGui::Text("%s%s | Ln %d, Col %d |",
			            GetDisplayFileName().c_str(),
			            state.unsavedChanges ? "*" : "",
			            cpos.mLine + 1,
			            cpos.mColumn + 1);

			editor.Render("TASEditor");
		}
		else
		{
			ImGui::TextDisabled("No file open. Use \"New\" or \"Open\" to get started.");
		}
	}
};

static TASEditor spt_TASEditor_feat;
