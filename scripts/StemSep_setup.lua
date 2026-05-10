-- StemSep_setup.lua
--
-- Builds the StemSep multi-out routing in one click:
--   - 1 source track named "StemSep" with 12 channels and the StemSep VST3 loaded.
--   - 5 child tracks (Drums, Bass, Guitar, Vocals, Other) each with a Track Receive
--     pulling its stem pair from the source (3/4, 5/6, 7/8, 9/10, 11/12) onto 1/2.
--   - The source track's master/parent send is disabled so channels 1/2 don't
--     double up against the children.
--
-- Install:
--   Linux:   ln -s "$PWD/scripts/StemSep_setup.lua" ~/.config/REAPER/Scripts/
--   Windows: copy scripts\StemSep_setup.lua %APPDATA%\REAPER\Scripts\
--
-- Run:
--   Reaper -> Actions -> Show action list -> ReaScript: Load... -> pick the file -> Run.
--   Or assign it a keyboard shortcut from the Action list.

local stems = {
  { name = "Drums",  srcChan = 2  },  -- I_SRCCHAN encoding: 0=ch1/2, 2=ch3/4, 4=ch5/6, ...
  { name = "Bass",   srcChan = 4  },
  { name = "Guitar", srcChan = 6  },
  { name = "Vocals", srcChan = 8  },
  { name = "Other",  srcChan = 10 },
}

reaper.Undo_BeginBlock()

local sourceIdx = reaper.CountTracks(0)
reaper.InsertTrackAtIndex(sourceIdx, true)
local source = reaper.GetTrack(0, sourceIdx)
reaper.GetSetMediaTrackInfo_String(source, "P_NAME", "StemSep", true)
reaper.SetMediaTrackInfo_Value(source, "I_NCHAN", 12)
reaper.SetMediaTrackInfo_Value(source, "B_MAINSEND", 0)

local fxIdx = reaper.TrackFX_AddByName(source, "StemSep", false, -1)
if fxIdx == -1 then
  reaper.ShowMessageBox(
    "StemSep VST3 not found.\n\n" ..
    "Install the plugin (~/.vst3 on Linux, C:\\Program Files\\Common Files\\VST3 on Windows) " ..
    "and rescan: Options -> Preferences -> Plug-ins -> VST -> Re-scan.",
    "StemSep setup", 0)
  reaper.Undo_EndBlock("StemSep multi-out setup (plugin missing)", -1)
  return
end

for i, stem in ipairs(stems) do
  local childIdx = sourceIdx + i
  reaper.InsertTrackAtIndex(childIdx, true)
  local child = reaper.GetTrack(0, childIdx)
  reaper.GetSetMediaTrackInfo_String(child, "P_NAME", stem.name, true)

  local sendIdx = reaper.CreateTrackSend(source, child)
  reaper.SetTrackSendInfo_Value(source, 0, sendIdx, "I_SRCCHAN", stem.srcChan)
  reaper.SetTrackSendInfo_Value(source, 0, sendIdx, "I_DSTCHAN", 0)
end

reaper.TrackList_AdjustWindows(false)
reaper.UpdateArrange()
reaper.Undo_EndBlock("StemSep multi-out setup", -1)
