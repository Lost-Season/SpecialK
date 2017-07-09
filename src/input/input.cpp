/**
 * This file is part of Special K.
 *
 * Special K is free software : you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by The Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * Special K is distributed in the hope that it will be useful,
 *
 * But WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Special K.
 *
 *   If not, see <http://www.gnu.org/licenses/>.
 *
**/
#define _CRT_SECURE_NO_WARNINGS
#define DIRECTINPUT_VERSION 0x0800

#define NOMINMAX

#include <SpecialK/input/input.h>
#include <SpecialK/window.h>
#include <SpecialK/console.h>

#include <SpecialK/utility.h>
#include <SpecialK/log.h>
#include <SpecialK/config.h>
#include <SpecialK/core.h>
#include <SpecialK/hooks.h>
#include <dinput.h>
#include <comdef.h>

#include <stdarg.h>

#include <imgui/imgui.h>


bool
SK_InputUtil_IsHWCursorVisible (void)
{
  CURSORINFO cursor_info;
             cursor_info.cbSize = sizeof (CURSORINFO);
  
  GetCursorInfo_Original (&cursor_info);
 
  return (cursor_info.flags & CURSOR_SHOWING);
}


#define SK_LOG_INPUT_CALL { static int  calls  = 0;                   { SK_LOG0 ( (L"[!] > Call #%lu: %hs", calls++, __FUNCTION__), L"Input Mgr." ); } }
#define SK_LOG_FIRST_CALL { static bool called = false; if (! called) { SK_LOG0 ( (L"[!] > First Call: %hs", __FUNCTION__), L"Input Mgr." ); called = true; } }


#define SK_HID_READ(type)  SK_HID_Backend.markRead  (type);
#define SK_HID_WRITE(type) SK_HID_Backend.markWrite (type);

#define SK_DI8_READ(type)  SK_DI8_Backend.markRead  (type);
#define SK_DI8_WRITE(type) SK_DI8_Backend.markWrite (type);

#define SK_RAWINPUT_READ(type)  SK_RawInput_Backend.markRead  (type);
#define SK_RAWINPUT_WRITE(type) SK_RawInput_Backend.markWrite (type);


//////////////////////////////////////////////////////////////
//
// HIDClass (Usermode)
//
//////////////////////////////////////////////////////////////
bool
SK_HID_FilterPreparsedData (PHIDP_PREPARSED_DATA pData)
{
  bool filter = false;

        HIDP_CAPS caps;
  const NTSTATUS  stat =
          HidP_GetCaps_Original (pData, &caps);

  if ( stat           == HIDP_STATUS_SUCCESS && 
       caps.UsagePage == HID_USAGE_PAGE_GENERIC )
  {
    switch (caps.Usage)
    {
      case HID_USAGE_GENERIC_GAMEPAD:
      case HID_USAGE_GENERIC_JOYSTICK:
      {
        SK_HID_READ (sk_input_dev_type::Gamepad)

        if (SK_ImGui_WantGamepadCapture ())
          filter = true;
      } break;

      case HID_USAGE_GENERIC_MOUSE:
      {
        SK_HID_READ (sk_input_dev_type::Mouse)
        if (SK_ImGui_WantMouseCapture ())
          filter = true;
      } break;

      case HID_USAGE_GENERIC_KEYBOARD:
      {
        SK_HID_READ (sk_input_dev_type::Keyboard)
        if (SK_ImGui_WantKeyboardCapture ())
          filter = true;
      } break;
    }
  }

  //SK_LOG0 ( ( L"HID Preparsed Data - Stat: %04x, UsagePage: %02x, Usage: %02x",
                //stat, caps.UsagePage, caps.Usage ),
              //L" HIDInput ");

  return filter;
}

PHIDP_PREPARSED_DATA* SK_HID_PreparsedDataP = nullptr;
PHIDP_PREPARSED_DATA  SK_HID_PreparsedData  = nullptr;

BOOLEAN
_Success_(return)
__stdcall
HidD_GetPreparsedData_Detour (
  _In_  HANDLE                HidDeviceObject,
  _Out_ PHIDP_PREPARSED_DATA *PreparsedData )
{
  SK_LOG_FIRST_CALL

  PHIDP_PREPARSED_DATA pData = nullptr;
  BOOLEAN              bRet  =
    HidD_GetPreparsedData_Original ( HidDeviceObject,
                                       &pData );

  if (bRet)
  {
    SK_HID_PreparsedDataP = PreparsedData;
    SK_HID_PreparsedData  = pData;

    if (SK_HID_FilterPreparsedData (pData) || config.input.gamepad.disable_ps4_hid)
      return FALSE;

    *PreparsedData   =  pData;
  }

  // Can't figure out how The Witness works yet, but it will bypass input blocking
  //   on HID using a PS4 controller unless we return FALSE here.
  //return FALSE;
  return bRet;
}

HidD_GetPreparsedData_pfn  HidD_GetPreparsedData_Original  = nullptr;
HidD_FreePreparsedData_pfn HidD_FreePreparsedData_Original = nullptr;
HidD_GetFeature_pfn        HidD_GetFeature_Original        = nullptr;
HidP_GetData_pfn           HidP_GetData_Original           = nullptr;
HidP_GetCaps_pfn           HidP_GetCaps_Original           = nullptr;
SetCursor_pfn              SetCursor_Original              = nullptr;

BOOLEAN
__stdcall
HidD_FreePreparsedData_Detour (
  _In_ PHIDP_PREPARSED_DATA PreparsedData )
{
  BOOLEAN bRet =
    HidD_FreePreparsedData_Original (PreparsedData);

  if (PreparsedData == SK_HID_PreparsedData)
    SK_HID_PreparsedData = nullptr;

  return bRet;
}

BOOLEAN
_Success_ (return)
__stdcall
HidD_GetFeature_Detour ( _In_  HANDLE HidDeviceObject,
                         _Out_ PVOID  ReportBuffer,
                         _In_  ULONG  ReportBufferLength )
{
  ////SK_HID_READ (sk_input_dev_type::Gamepad)

  bool                 filter = false;
  PHIDP_PREPARSED_DATA pData  = nullptr;

  if (HidD_GetPreparsedData_Original (HidDeviceObject, &pData))
  {
    if (SK_HID_FilterPreparsedData (pData))
      filter = true;

    HidD_FreePreparsedData_Original (pData);
  }

  if (! filter)
    return HidD_GetFeature_Original ( HidDeviceObject, ReportBuffer, ReportBufferLength );

  return FALSE;
}

NTSTATUS
__stdcall
HidP_GetData_Detour (
  _In_    HIDP_REPORT_TYPE     ReportType,
  _Out_   PHIDP_DATA           DataList,
  _Inout_ PULONG               DataLength,
  _In_    PHIDP_PREPARSED_DATA PreparsedData,
  _In_    PCHAR                Report,
  _In_    ULONG                ReportLength )
{
  SK_LOG_FIRST_CALL

  NTSTATUS ret =
    HidP_GetData_Original ( ReportType, DataList,
                              DataLength, PreparsedData,
                                Report, ReportLength );


  // De we want block this I/O?
  bool filter = false;

  if ( ret == HIDP_STATUS_SUCCESS && ( ReportType == HidP_Input || ReportType == HidP_Output ))
  {
    // This will classify the data for us, so don't record this event yet.
    filter = SK_HID_FilterPreparsedData (PreparsedData);
  }


  if (! filter)
    return ret;

  else {
    memset (DataList, *DataLength, 0);
           *DataLength           = 0;
  }

  return ret;
}

void
SK_Input_HookHID (void)
{
  if (! config.input.gamepad.hook_hid)
    return;

  static volatile LONG hooked = FALSE;

  if (! InterlockedExchangeAdd (&hooked, 0))
  {
    SK_LOG0 ( ( L"Game uses HID, installing input hooks..." ),
                L"   Input  " );

    SK_CreateDLLHook2 ( L"HID.DLL", "HidP_GetData",
                          HidP_GetData_Detour,
                (LPVOID*)&HidP_GetData_Original );

    SK_CreateDLLHook2 ( L"HID.DLL", "HidD_GetPreparsedData",
                          HidD_GetPreparsedData_Detour,
                (LPVOID*)&HidD_GetPreparsedData_Original );

    SK_CreateDLLHook2 ( L"HID.DLL", "HidD_FreePreparsedData",
                          HidD_FreePreparsedData_Detour,
                (LPVOID*)&HidD_FreePreparsedData_Original );

    SK_CreateDLLHook2 ( L"HID.DLL", "HidD_GetFeature",
                          HidD_GetFeature_Detour,
                (LPVOID*)&HidD_GetFeature_Original );

    HidP_GetCaps_Original =
      (HidP_GetCaps_pfn)GetProcAddress ( GetModuleHandle (L"HID.DLL"),
                                           "HidP_GetCaps" );

    MH_ApplyQueued ();

    if (HidP_GetData_Original != nullptr)
      InterlockedIncrement (&hooked);
  }
}

void
SK_Input_PreHookHID (void)
{
  if (! config.input.gamepad.hook_hid)
    return;

  static sk_import_test_s tests [] = { { "hid.dll", false } };

  SK_TestImports (GetModuleHandle (nullptr), tests, 1);

  if (tests [0].used)// || GetModuleHandle (L"hid.dll"))
  {
    SK_Input_HookHID ();
  }
}


//////////////////////////////////////////////////////////////////////////////////
//
// Raw Input
//
//////////////////////////////////////////////////////////////////////////////////
std::vector <RAWINPUTDEVICE> raw_devices;   // ALL devices, this is the list as Windows would give it to the game

std::vector <RAWINPUTDEVICE> raw_mice;      // View of only mice
std::vector <RAWINPUTDEVICE> raw_keyboards; // View of only keyboards
std::vector <RAWINPUTDEVICE> raw_gamepads;  // View of only gamepads

struct
{

  struct
  {
    bool active          = false;
    bool legacy_messages = false;
  } mouse,
    keyboard;

} raw_overrides;

typedef UINT (WINAPI *GetRegisteredRawInputDevices_pfn)(
  _Out_opt_ PRAWINPUTDEVICE pRawInputDevices,
  _Inout_   PUINT           puiNumDevices,
  _In_      UINT            cbSize );

GetRegisteredRawInputDevices_pfn GetRegisteredRawInputDevices_Original = nullptr;

// Returns all mice, in their override state (if applicable)
std::vector <RAWINPUTDEVICE>
SK_RawInput_GetMice (bool* pDifferent = nullptr)
{
  bool different = false;

  if (raw_overrides.mouse.active)
  {
    std::vector <RAWINPUTDEVICE> overrides;

    // Aw, the game doesn't have any mice -- let's fix that.
    if (raw_mice.size () == 0)
    {
      //raw_devices.push_back (RAWINPUTDEVICE { HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, 0x00, NULL });
      //raw_mice.push_back    (RAWINPUTDEVICE { HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, 0x00, NULL });
      //raw_overrides.mouse.legacy_messages = true;
    }

    for (RAWINPUTDEVICE it : raw_mice)
    {
      HWND hWnd = it.hwndTarget;

      if (raw_overrides.mouse.legacy_messages)
      {
        different |= (it.dwFlags & RIDEV_NOLEGACY) != 0;
        it.dwFlags   &= ~(RIDEV_NOLEGACY | RIDEV_APPKEYS | RIDEV_REMOVE);
        it.dwFlags   &= ~RIDEV_CAPTUREMOUSE;
        it.hwndTarget = hWnd;
        RegisterRawInputDevices_Original ( &it, 1, sizeof RAWINPUTDEVICE );
      }

      else
      {
        different |= (it.dwFlags & RIDEV_NOLEGACY) == 0;
        it.dwFlags              |= RIDEV_NOLEGACY;
        RegisterRawInputDevices_Original ( &it, 1, sizeof RAWINPUTDEVICE );
      }
    
      overrides.push_back (it);
    }

    if (pDifferent != nullptr)
      *pDifferent = different;

    return overrides;
  }

  else
  {
    if (pDifferent != nullptr)
       *pDifferent = false;

    return raw_mice;
  }
}

// Returns all keyboards, in their override state (if applicable)
std::vector <RAWINPUTDEVICE>
SK_RawInput_GetKeyboards (bool* pDifferent = nullptr)
{
  bool different = false;

  if (raw_overrides.keyboard.active)
  {
    std::vector <RAWINPUTDEVICE> overrides;

    // Aw, the game doesn't have any mice -- let's fix that.
    if (raw_keyboards.size () == 0)
    {
      //raw_devices.push_back   (RAWINPUTDEVICE { HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD, 0x00, NULL });
      //raw_keyboards.push_back (RAWINPUTDEVICE { HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD, 0x00, NULL });
      //raw_overrides.keyboard.legacy_messages = true;
    }

    for (auto& it : raw_keyboards)
    {
      if (raw_overrides.keyboard.legacy_messages)
      {
        different |= ((it).dwFlags & RIDEV_NOLEGACY) != 0;
        (it).dwFlags &= ~(RIDEV_NOLEGACY | RIDEV_APPKEYS);
      }

      else
      {
        different |= ((it).dwFlags & RIDEV_NOLEGACY) == 0;
        (it).dwFlags |=   RIDEV_NOLEGACY | RIDEV_APPKEYS;
      }

      overrides.push_back (it);
    }

    if (pDifferent != nullptr)
      *pDifferent = different;

    return overrides;
  }

  else
  {
    if (pDifferent != nullptr)
      *pDifferent = false;

    //(it).dwFlags &= ~(RIDEV_NOLEGACY | RIDEV_APPKEYS);

    return raw_keyboards;
  }
}

// Temporarily override game's preferences for input device window message generation
bool
SK_RawInput_EnableLegacyMouse (bool enable)
{
  if (! raw_overrides.mouse.active)
  {
    raw_overrides.mouse.active          = true;
    raw_overrides.mouse.legacy_messages = enable;

    // XXX: In the one in a million chance that a game supports multiple mice ...
    //        Special K doesn't distinguish one from the other :P
    //

    std::vector <RAWINPUTDEVICE> device_override;

    bool different = false;

    std::vector <RAWINPUTDEVICE> mice = SK_RawInput_GetMice (&different);

    for (auto& it : raw_keyboards) device_override.push_back (it);
    for (auto& it : raw_gamepads)  device_override.push_back (it);
    for (auto& it : mice)          device_override.push_back (it);

//    dll_log.Log (L"%lu mice are now legacy...", mice.size ());

    RegisterRawInputDevices_Original (
      device_override.data (),
        static_cast <UINT> (device_override.size ()),
          sizeof RAWINPUTDEVICE
    );

    return different;
  }

  return false;
}

// Restore the game's original setting
void
SK_RawInput_RestoreLegacyMouse (void)
{
  if (raw_overrides.mouse.active)
  {
    raw_overrides.mouse.active = false;

    RegisterRawInputDevices_Original (
      raw_devices.data (),
        static_cast <UINT> (raw_devices.size ()),
          sizeof RAWINPUTDEVICE
    );
  }
}

// Temporarily override game's preferences for input device window message generation
bool
SK_RawInput_EnableLegacyKeyboard (bool enable)
{
  if (! raw_overrides.keyboard.active)
  {
    raw_overrides.keyboard.active          = true;
    raw_overrides.keyboard.legacy_messages = enable;

    std::vector <RAWINPUTDEVICE> device_override;

    bool different = false;

    std::vector <RAWINPUTDEVICE> keyboards = SK_RawInput_GetKeyboards (&different);

    for (auto& it : keyboards)    device_override.push_back (it);
    for (auto& it : raw_gamepads) device_override.push_back (it);
    for (auto& it : raw_mice)     device_override.push_back (it);

    RegisterRawInputDevices_Original (
      device_override.data (),
        static_cast <UINT> (device_override.size ()),
          sizeof RAWINPUTDEVICE
    );

    return different;
  }

  return false;
}

// Restore the game's original setting
void
SK_RawInput_RestoreLegacyKeyboard (void)
{
  if (raw_overrides.keyboard.active)
  {
    raw_overrides.keyboard.active = false;

    RegisterRawInputDevices_Original (
      raw_devices.data (),
        static_cast <UINT> (raw_devices.size ()),
          sizeof RAWINPUTDEVICE
    );
  }
}

std::vector <RAWINPUTDEVICE>&
SK_RawInput_GetRegisteredGamepads (void)
{
  return raw_gamepads;
}


// Given a complete list of devices in raw_devices, sub-divide into category for
//   quicker device filtering.
void
SK_RawInput_ClassifyDevices (void)
{
  raw_mice.clear      ();
  raw_keyboards.clear ();
  raw_gamepads.clear  ();

  for (auto& it : raw_devices)
  {
    if ((it).usUsagePage == HID_USAGE_PAGE_GENERIC)
    {
      switch ((it).usUsage)
      {
        case HID_USAGE_GENERIC_MOUSE:
          raw_mice.push_back       (it);
          break;

        case HID_USAGE_GENERIC_KEYBOARD:
          raw_keyboards.push_back  (it);
          break;

        case HID_USAGE_GENERIC_JOYSTICK: // Joystick
        case HID_USAGE_GENERIC_GAMEPAD:  // Gamepad
          raw_gamepads.push_back   (it);
          break;

        default:
          // UH OH, what the heck is this device?
          break;
      }
    }
  }
}

UINT
SK_RawInput_PopulateDeviceList (void)
{
  DWORD            dwLastError = GetLastError ();
  RAWINPUTDEVICE*  pDevices    = nullptr;
  UINT            uiNumDevices = 0;

  UINT ret =
    GetRegisteredRawInputDevices_Original (pDevices, &uiNumDevices, sizeof RAWINPUTDEVICE);

  assert (ret == -1);

  SetLastError (dwLastError);

  if (uiNumDevices != 0)
  {
    pDevices = new RAWINPUTDEVICE [uiNumDevices + 1];

    GetRegisteredRawInputDevices_Original (pDevices, &uiNumDevices, sizeof RAWINPUTDEVICE);

    raw_devices.clear ();

    for (UINT i = 0; i < uiNumDevices; i++)
      raw_devices.push_back (pDevices [i]);

    SK_RawInput_ClassifyDevices ();

    delete [] pDevices;
  }

  return uiNumDevices;
}

UINT WINAPI GetRegisteredRawInputDevices_Detour (
  _Out_opt_ PRAWINPUTDEVICE pRawInputDevices,
  _Inout_   PUINT           puiNumDevices,
  _In_      UINT            cbSize )
{
  SK_LOG_FIRST_CALL

  assert (cbSize == sizeof RAWINPUTDEVICE);

  // On the first call to this function, we will need to query this stuff.
  static bool init = false;

  if (! init)
  {
    SK_RawInput_PopulateDeviceList ();
    init = true;
  }


  if (*puiNumDevices < static_cast <UINT> (raw_devices.size ()))
  {
      *puiNumDevices = static_cast <UINT> (raw_devices.size ());

    SetLastError (ERROR_INSUFFICIENT_BUFFER);

    return -1;
  }

  int idx = 0;
  for (auto& it : raw_devices)
  {
    if (pRawInputDevices)
      pRawInputDevices [idx++] = it;
    else
      idx++;
  }

  return idx;
}

BOOL WINAPI RegisterRawInputDevices_Detour (
  _In_ PCRAWINPUTDEVICE pRawInputDevices,
  _In_ UINT             uiNumDevices,
  _In_ UINT             cbSize )
{
  SK_LOG_FIRST_CALL

  if (cbSize != sizeof RAWINPUTDEVICE)
  {
    dll_log.Log ( L"[ RawInput ] RegisterRawInputDevices has wrong "
                  L"structure size (%lu bytes), expected: %zu",
                    cbSize,
                      sizeof RAWINPUTDEVICE );

    return
      RegisterRawInputDevices_Original ( pRawInputDevices,
                                           uiNumDevices,
                                             cbSize );
  }

  raw_devices.clear ();

  RAWINPUTDEVICE* pDevices = nullptr;

  if (pRawInputDevices && uiNumDevices > 0)
    pDevices = new RAWINPUTDEVICE [uiNumDevices];

  // The devices that we will pass to Windows after any overrides are applied
  std::vector <RAWINPUTDEVICE> actual_device_list;

  if (pDevices != nullptr)
  {
    // We need to continue receiving window messages for the console to work
    for (unsigned int i = 0; i < uiNumDevices; i++)
    {
      pDevices [i] = pRawInputDevices [i];
      raw_devices.push_back (pDevices [i]);
    }

    SK_RawInput_ClassifyDevices ();
  }

  std::vector <RAWINPUTDEVICE> override_keyboards = SK_RawInput_GetKeyboards          ();
  std::vector <RAWINPUTDEVICE> override_mice      = SK_RawInput_GetMice               ();
  std::vector <RAWINPUTDEVICE> override_gamepads  = SK_RawInput_GetRegisteredGamepads ();

  for (auto& it : override_keyboards) actual_device_list.push_back (it);
  for (auto& it : override_mice)      actual_device_list.push_back (it);
  for (auto& it : override_gamepads)  actual_device_list.push_back (it);

  BOOL bRet =
    pDevices != nullptr ?
      RegisterRawInputDevices_Original ( actual_device_list.data   (),
                       static_cast <UINT> (actual_device_list.size () ),
                                             cbSize ) :
                FALSE;

  if (pDevices)
    delete [] pDevices;

  return bRet;
}

RegisterRawInputDevices_pfn RegisterRawInputDevices_Original = nullptr;
GetRawInputData_pfn         GetRawInputData_Original         = nullptr;
GetRawInputBuffer_pfn       GetRawInputBuffer_Original       = nullptr;

//
// This function has never once been encountered after debugging > 100 games,
//   it is VERY likely this logic is broken since no known game uses RawInput
//     for buffered reads.
//
UINT
WINAPI
GetRawInputBuffer_Detour (_Out_opt_ PRAWINPUT pData,
                          _Inout_   PUINT     pcbSize,
                          _In_      UINT      cbSizeHeader)
{
  SK_LOG_FIRST_CALL

  if (SK_ImGui_Visible)
  {
    ImGuiIO& io =
      ImGui::GetIO ();

    bool filter = false;

    // Unconditional
    if (config.input.ui.capture)
      filter = true;

    // Only specific types
    if (io.WantCaptureKeyboard || io.WantCaptureMouse)
      filter = true;

    if (filter)
    {
      if (pData != nullptr)
      {
        ZeroMemory (pData, *pcbSize);
        const int max_items = (sizeof RAWINPUT / *pcbSize);
              int count     =                            0;
        uint8_t *pTemp      = (uint8_t *)
                                  new RAWINPUT [max_items];
        uint8_t *pInput     =                        pTemp;
        uint8_t *pOutput    =             (uint8_t *)pData;
        UINT     cbSize     =                     *pcbSize;
                  *pcbSize  =                            0;

        int       temp_ret  =
          GetRawInputBuffer_Original ( (RAWINPUT *)pTemp, &cbSize, cbSizeHeader );

        for (int i = 0; i < temp_ret; i++)
        {
          RAWINPUT* pItem = (RAWINPUT *)pInput;

          bool  remove = false;
          int  advance = pItem->header.dwSize;

          switch (pItem->header.dwType)
          {
            case RIM_TYPEKEYBOARD:
              SK_RAWINPUT_READ (sk_input_dev_type::Keyboard)
              if (SK_ImGui_WantKeyboardCapture ())
                remove = true;
              break;

            case RIM_TYPEMOUSE:
              SK_RAWINPUT_READ (sk_input_dev_type::Mouse)
              if (SK_ImGui_WantMouseCapture ())
                remove = true;
              break;

            default:
              SK_RAWINPUT_READ (sk_input_dev_type::Gamepad)
              if (SK_ImGui_WantGamepadCapture ())
                remove = true;
              break;
          }

          if (config.input.ui.capture)
            remove = true;

          if (! remove)
          {
            memcpy (pOutput, pItem, pItem->header.dwSize);
             pOutput += advance;
                        ++count;
            *pcbSize += advance;
          }
          
          pInput += advance;
        }

        delete [] pTemp;

        return count;
      }
    }
  }

  return GetRawInputBuffer_Original (pData, pcbSize, cbSizeHeader);
}

UINT
WINAPI
SK_ImGui_ProcessRawInput (_In_      HRAWINPUT hRawInput,
                          _In_      UINT      uiCommand,
                          _Out_opt_ LPVOID    pData,
                          _Inout_   PUINT     pcbSize,
                          _In_      UINT      cbSizeHeader,
                                    BOOL      self);

UINT
WINAPI
GetRawInputData_Detour (_In_      HRAWINPUT hRawInput,
                        _In_      UINT      uiCommand,
                        _Out_opt_ LPVOID    pData,
                        _Inout_   PUINT     pcbSize,
                        _In_      UINT      cbSizeHeader)
{
  SK_LOG_FIRST_CALL

  //if (SK_ImGui_WantGamepadCapture ())
    return SK_ImGui_ProcessRawInput (hRawInput, uiCommand, pData, pcbSize, cbSizeHeader, false);

 //return GetRawInputData_Original (hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
}





#define DINPUT8_CALL(_Ret, _Call) {                                     \
  dll_log.LogEx (true, L"[   Input  ]  Calling original function: ");   \
  (_Ret) = (_Call);                                                     \
  _com_error err ((_Ret));                                              \
  if ((_Ret) != S_OK)                                                   \
    dll_log.LogEx (false, L"(ret=0x%04x - %s)\n", err.WCode (),         \
                                                  err.ErrorMessage ()); \
  else                                                                  \
    dll_log.LogEx (false, L"(ret=S_OK)\n");                             \
}

///////////////////////////////////////////////////////////////////////////////
//
// DirectInput 8
//
///////////////////////////////////////////////////////////////////////////////
typedef HRESULT (WINAPI *IDirectInput8_CreateDevice_pfn)(
  IDirectInput8       *This,
  REFGUID              rguid,
  LPDIRECTINPUTDEVICE *lplpDirectInputDevice,
  LPUNKNOWN            pUnkOuter
);

typedef HRESULT (WINAPI *IDirectInputDevice8_GetDeviceState_pfn)(
  LPDIRECTINPUTDEVICE  This,
  DWORD                cbData,
  LPVOID               lpvData
);

typedef HRESULT (WINAPI *IDirectInputDevice8_SetCooperativeLevel_pfn)(
  LPDIRECTINPUTDEVICE  This,
  HWND                 hwnd,
  DWORD                dwFlags
);

IDirectInput8_CreateDevice_pfn
        IDirectInput8_CreateDevice_Original               = nullptr;

IDirectInputDevice8_GetDeviceState_pfn
        IDirectInputDevice8_GetDeviceState_MOUSE_Original = nullptr;

IDirectInputDevice8_GetDeviceState_pfn
        IDirectInputDevice8_GetDeviceState_KEYBOARD_Original = nullptr;

IDirectInputDevice8_GetDeviceState_pfn
        IDirectInputDevice8_GetDeviceState_GAMEPAD_Original = nullptr;

IDirectInputDevice8_SetCooperativeLevel_pfn
        IDirectInputDevice8_SetCooperativeLevel_Original  = nullptr;


struct SK_DI8_Keyboard _dik;
struct SK_DI8_Mouse    _dim;


__declspec (noinline)
SK_DI8_Keyboard*
WINAPI
SK_Input_GetDI8Keyboard (void)
{
  return &_dik;
}

__declspec (noinline)
SK_DI8_Mouse*
WINAPI
SK_Input_GetDI8Mouse (void)
{
  return &_dim;
}

__declspec (noinline)
bool
WINAPI
SK_Input_DI8Mouse_Acquire (SK_DI8_Mouse* pMouse)
{
  if (pMouse == nullptr && _dim.pDev != nullptr)
    pMouse = &_dim;

  if (pMouse != nullptr)
  {
    IDirectInputDevice8_SetCooperativeLevel_Original (
      pMouse->pDev,
        game_window.hWnd,
          pMouse->coop_level
    );

    return true;
  }

  return false;
}

__declspec (noinline)
bool
WINAPI
SK_Input_DI8Mouse_Release (SK_DI8_Mouse* pMouse)
{
  if (pMouse == nullptr && _dim.pDev != nullptr)
    pMouse = &_dim;

  if (pMouse != nullptr)
  {
    IDirectInputDevice8_SetCooperativeLevel_Original (
      pMouse->pDev,
        game_window.hWnd,
          (pMouse->coop_level & (~DISCL_EXCLUSIVE)) | DISCL_NONEXCLUSIVE
    );

    return true;
  }

  return false;
}


#include <SpecialK/input/xinput.h>


XINPUT_STATE di8_to_xi;

XINPUT_STATE
SK_DI8_TranslateToXInput (DIJOYSTATE* pJoy)
{
  static DWORD dwPacket = 0;

  ZeroMemory (&di8_to_xi.Gamepad, sizeof (XINPUT_STATE::Gamepad));

  //
  // Hard-coded mappings for DualShock 4 -> XInput
  //

  if (pJoy->rgbButtons [ 9])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_START;

  if (pJoy->rgbButtons [ 8])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;

  if (pJoy->rgbButtons [10])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;

  if (pJoy->rgbButtons [11])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

  if (pJoy->rgbButtons [ 6])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_TRIGGER;

  if (pJoy->rgbButtons [ 7])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_TRIGGER;

  if (pJoy->rgbButtons [ 4])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;

  if (pJoy->rgbButtons [ 5])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;

  if (pJoy->rgbButtons [ 1])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_A;

  if (pJoy->rgbButtons [ 2])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_B;

  if (pJoy->rgbButtons [ 0])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_X;

  if (pJoy->rgbButtons [ 3])
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_Y;

  if (pJoy->rgdwPOV [0] == 0)
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;

  if (pJoy->rgdwPOV [0] == 9000)
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;

  if (pJoy->rgdwPOV [0] == 18000)
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;

  if (pJoy->rgdwPOV [0] == 27000)
    di8_to_xi.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;

  di8_to_xi.Gamepad.sThumbLX      =  (SHORT)((float)MAXSHORT * ((float)pJoy->lX / 255.0f));
  di8_to_xi.Gamepad.sThumbLY      = -(SHORT)((float)MAXSHORT * ((float)pJoy->lY / 255.0f));

  di8_to_xi.Gamepad.sThumbRX      =  (SHORT)((float)MAXSHORT * ((float)pJoy->lZ  / 255.0f));
  di8_to_xi.Gamepad.sThumbRY      = -(SHORT)((float)MAXSHORT * ((float)pJoy->lRz / 255.0f));

  di8_to_xi.Gamepad.bLeftTrigger  =   (BYTE)((float)MAXBYTE  * ((float)pJoy->lRx / 255.0f));
  di8_to_xi.Gamepad.bRightTrigger =   (BYTE)((float)MAXBYTE  * ((float)pJoy->lRy / 255.0f));

  di8_to_xi.dwPacketNumber = dwPacket++;

  return di8_to_xi;
}

HRESULT
WINAPI
IDirectInputDevice8_GetDeviceState_Detour ( LPDIRECTINPUTDEVICE        This,
                                            DWORD                      cbData,
                                            LPVOID                     lpvData )
{
  SK_LOG_FIRST_CALL

  SK_LOG4 ( ( L" DirectInput 8 - GetDeviceState: cbData = %lu",
                cbData ),
              L"Direct Inp" );

  HRESULT hr = S_OK;

  if (SUCCEEDED (hr) && lpvData != nullptr)
  {
    if (cbData == sizeof DIJOYSTATE2) 
    {
      SK_DI8_READ (sk_input_dev_type::Gamepad)
      static DIJOYSTATE2 last_state;

      DIJOYSTATE2* out = (DIJOYSTATE2 *)lpvData;

      SK_DI8_TranslateToXInput ((DIJOYSTATE *)out);

      if (nav_usable)
      {
        memcpy (out, &last_state, cbData);

        out->rgdwPOV [0] = -1;
        out->rgdwPOV [1] = -1;
        out->rgdwPOV [2] = -1;
        out->rgdwPOV [3] = -1;
      } else
        memcpy (&last_state, out, cbData);
    }

    else if (cbData == sizeof DIJOYSTATE) 
    {
      SK_DI8_READ (sk_input_dev_type::Gamepad)

      //dll_log.Log (L"Joy");

      static DIJOYSTATE last_state;

      DIJOYSTATE* out = (DIJOYSTATE *)lpvData;

      SK_DI8_TranslateToXInput (out);

      if (nav_usable)
      {
        memcpy (out, &last_state, cbData);

        out->rgdwPOV [0] = -1;
        out->rgdwPOV [1] = -1;
        out->rgdwPOV [2] = -1;
        out->rgdwPOV [3] = -1;
      }
      else
        memcpy (&last_state, out, cbData);

#if 0
      XINPUT_STATE xis;
      SK_XInput_PollController (0, &xis);

      out->rgbButtons [ 9] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_START          ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [ 8] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_BACK           ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [10] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB     ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [11] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB    ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [ 6] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_TRIGGER   ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [ 7] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_TRIGGER  ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [ 4] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER  ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [ 5] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [ 1] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_A              ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [ 2] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_B              ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [ 0] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_X              ) != 0x0 ? 0xFF : 0x00);
      out->rgbButtons [ 3] = (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_Y              ) != 0x0 ? 0xFF : 0x00);

      out->rgdwPOV [0] += (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP           ) != 0x0 ?      0 : 0);
      out->rgdwPOV [0] += (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT        ) != 0x0 ?  90000 : 0);
      out->rgdwPOV [0] += (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN         ) != 0x0 ? 180000 : 0);
      out->rgdwPOV [0] += (( xis.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT         ) != 0x0 ? 270000 : 0);

      if (out->rgdwPOV [0] == 0)
        out->rgdwPOV [0] = -1;
#endif
    }

    else if (This == _dik.pDev || cbData == 256)
    {
      SK_DI8_READ (sk_input_dev_type::Keyboard)

      if (SK_ImGui_WantKeyboardCapture () && lpvData != nullptr)
        memset (lpvData, 0, cbData);
    }

    else if ( cbData == sizeof (DIMOUSESTATE2) ||
              cbData == sizeof (DIMOUSESTATE)  )
    {
      SK_DI8_READ (sk_input_dev_type::Mouse)

      //dll_log.Log (L"Mouse");

      //((DIMOUSESTATE *)lpvData)->lZ += InterlockedAdd      (&SK_Input_GetDI8Mouse ()->delta_z, 0);
                                       //InterlockedExchange (&SK_Input_GetDI8Mouse ()->delta_z, 0);

      if (SK_ImGui_WantMouseCapture ())
      {
        switch (cbData)
        {
          case sizeof (DIMOUSESTATE2):
            ((DIMOUSESTATE2 *)lpvData)->lX = 0;
            ((DIMOUSESTATE2 *)lpvData)->lY = 0;
            ((DIMOUSESTATE2 *)lpvData)->lZ = 0;
            memset (((DIMOUSESTATE2 *)lpvData)->rgbButtons, 0, 8);
            break;

          case sizeof (DIMOUSESTATE):
            ((DIMOUSESTATE *)lpvData)->lX = 0;
            ((DIMOUSESTATE *)lpvData)->lY = 0;
            ((DIMOUSESTATE *)lpvData)->lZ = 0;
            memset (((DIMOUSESTATE *)lpvData)->rgbButtons, 0, 4);
            break;
        }
      }
    }
  }

  return hr;
}

bool
SK_DInput8_HasKeyboard (void)
{
  return (_dik.pDev && IDirectInputDevice8_SetCooperativeLevel_Original);
}
bool
SK_DInput8_BlockWindowsKey (bool block)
{
  DWORD dwFlags =
    block ? DISCL_NOWINKEY : 0x0;

  dwFlags &= ~DISCL_EXCLUSIVE;
  dwFlags &= ~DISCL_BACKGROUND;

  dwFlags |= DISCL_NONEXCLUSIVE;
  dwFlags |= DISCL_FOREGROUND;

  if (SK_DInput8_HasKeyboard ())
    IDirectInputDevice8_SetCooperativeLevel_Original (_dik.pDev, game_window.hWnd, dwFlags);
  else
    return false;

  return block;
}

bool
SK_DInput8_HasMouse (void)
{
  return (_dim.pDev && IDirectInputDevice8_SetCooperativeLevel_Original);
}

//
// TODO: Create a wrapper instead of flat hooks like this, this won't work when
//         multiple hardware vendor devices are present.
//

HRESULT
WINAPI
IDirectInputDevice8_GetDeviceState_MOUSE_Detour ( LPDIRECTINPUTDEVICE        This,
                                                  DWORD                      cbData,
                                                  LPVOID                     lpvData )
{
  HRESULT hr = IDirectInputDevice8_GetDeviceState_MOUSE_Original ( This, cbData, lpvData );

  if (SUCCEEDED (hr))
    IDirectInputDevice8_GetDeviceState_Detour ( This, cbData, lpvData );

  return hr;
}

HRESULT
WINAPI
IDirectInputDevice8_GetDeviceState_KEYBOARD_Detour ( LPDIRECTINPUTDEVICE        This,
                                                     DWORD                      cbData,
                                                     LPVOID                     lpvData )
{
  HRESULT hr = IDirectInputDevice8_GetDeviceState_KEYBOARD_Original ( This, cbData, lpvData );

  if (SUCCEEDED (hr))
    IDirectInputDevice8_GetDeviceState_Detour ( This, cbData, lpvData );

  return hr;
}

HRESULT
WINAPI
IDirectInputDevice8_GetDeviceState_GAMEPAD_Detour ( LPDIRECTINPUTDEVICE        This,
                                                    DWORD                      cbData,
                                                    LPVOID                     lpvData )
{
  HRESULT hr = IDirectInputDevice8_GetDeviceState_GAMEPAD_Original ( This, cbData, lpvData );

  if (SUCCEEDED (hr))
    IDirectInputDevice8_GetDeviceState_Detour ( This, cbData, lpvData );

  return hr;
}



HRESULT
WINAPI
IDirectInputDevice8_SetCooperativeLevel_Detour ( LPDIRECTINPUTDEVICE  This,
                                                 HWND                 hwnd,
                                                 DWORD                dwFlags )
{
  if (config.input.keyboard.block_windows_key)
    dwFlags |= DISCL_NOWINKEY;

  HRESULT hr =
    IDirectInputDevice8_SetCooperativeLevel_Original (This, hwnd, dwFlags);

  if (SUCCEEDED (hr))
  {
    // Mouse
    if (This == _dim.pDev)
      _dim.coop_level = dwFlags;

    // Keyboard   (why do people use DirectInput for keyboards? :-\)
    else if (This == _dik.pDev)
      _dik.coop_level = dwFlags;

    // Anything else is probably not important
  }

  if (SK_ImGui_WantMouseCapture ())
  {
    dwFlags &= ~DISCL_EXCLUSIVE;

    IDirectInputDevice8_SetCooperativeLevel_Original (This, hwnd, dwFlags);
  }

  return hr;
}

HRESULT
WINAPI
IDirectInput8_CreateDevice_Detour ( IDirectInput8       *This,
                                    REFGUID              rguid,
                                    LPDIRECTINPUTDEVICE *lplpDirectInputDevice,
                                    LPUNKNOWN            pUnkOuter )
{
  const wchar_t* wszDevice = (rguid == GUID_SysKeyboard)   ? L"Default System Keyboard" :
                                (rguid == GUID_SysMouse)   ? L"Default System Mouse"    :  
                                  (rguid == GUID_Joystick) ? L"Gamepad / Joystick"      :
                                                           L"Other Device";

  dll_log.Log ( L"[   Input  ][!] IDirectInput8::CreateDevice (%ph, %s, %ph, %ph)",
                   This,
                     wszDevice,
                       lplpDirectInputDevice,
                         pUnkOuter );

  HRESULT hr;
  DINPUT8_CALL ( hr,
                  IDirectInput8_CreateDevice_Original ( This,
                                                         rguid,
                                                          lplpDirectInputDevice,
                                                           pUnkOuter ) );

  if (SUCCEEDED (hr))
  {
    void** vftable = *(void***)*lplpDirectInputDevice;

    //
    // This weird hack is necessary for EverQuest; crazy game hooks itself to try and thwart
    //   macro programs.
    //
    /*
    if (rguid == GUID_SysMouse && _dim.pDev == nullptr)
    {
      SK_CreateFuncHook ( L"IDirectInputDevice8::GetDeviceState",
                           vftable [9],
                           IDirectInputDevice8_GetDeviceState_MOUSE_Detour,
                (LPVOID *)&IDirectInputDevice8_GetDeviceState_MOUSE_Original );
      MH_QueueEnableHook (vftable [9]);
    }

    else if (rguid == GUID_SysKeyboard && _dik.pDev == nullptr)
    {
      SK_CreateFuncHook ( L"IDirectInputDevice8::GetDeviceState",
                           vftable [9],
                           IDirectInputDevice8_GetDeviceState_KEYBOARD_Detour,
                (LPVOID *)&IDirectInputDevice8_GetDeviceState_KEYBOARD_Original );
      MH_QueueEnableHook (vftable [9]);
    }

    else if (rguid != GUID_SysMouse && rguid != GUID_SysKeyboard)
    {
    */
    if (IDirectInputDevice8_GetDeviceState_GAMEPAD_Original == nullptr)
    {
      SK_CreateFuncHook ( L"IDirectInputDevice8::GetDeviceState",
                           vftable [9],
                           IDirectInputDevice8_GetDeviceState_GAMEPAD_Detour,
                (LPVOID *)&IDirectInputDevice8_GetDeviceState_GAMEPAD_Original );
      MH_QueueEnableHook (vftable [9]);
    }
    //}

    if (! IDirectInputDevice8_SetCooperativeLevel_Original)
    {
      SK_CreateFuncHook ( L"IDirectInputDevice8::SetCooperativeLevel",
                           vftable [13],
                           IDirectInputDevice8_SetCooperativeLevel_Detour,
                 (LPVOID*)&IDirectInputDevice8_SetCooperativeLevel_Original );
      MH_QueueEnableHook (vftable [13]);
    }

    MH_ApplyQueued ();

    if (rguid == GUID_SysMouse)
    {
      _dim.pDev = *lplpDirectInputDevice;
    }
    else if (rguid == GUID_SysKeyboard)
      _dik.pDev = *lplpDirectInputDevice;
  }

#if 0
  if (SUCCEEDED (hr) && lplpDirectInputDevice != nullptr)
  {
    DWORD dwFlag = DISCL_FOREGROUND | DISCL_NONEXCLUSIVE;

    if (config.input.block_windows)
      dwFlag |= DISCL_NOWINKEY;

    (*lplpDirectInputDevice)->SetCooperativeLevel (SK_GetGameWindow (), dwFlag);
  }
#endif

  return hr;
}

typedef HRESULT (WINAPI *DirectInput8Create_pfn)(
 HINSTANCE hinst,
 DWORD     dwVersion,
 REFIID    riidltf,
 LPVOID*   ppvOut,
 LPUNKNOWN punkOuter
);

DirectInput8Create_pfn DirectInput8Create_Original = nullptr;

HRESULT
WINAPI
DirectInput8Create_Detour (
  HINSTANCE hinst,
  DWORD     dwVersion,
  REFIID    riidltf,
  LPVOID*   ppvOut,
  LPUNKNOWN punkOuter
)
{
  HRESULT hr = E_NOINTERFACE;

  if ( SUCCEEDED (
         (hr = DirectInput8Create_Original (hinst, dwVersion, riidltf, ppvOut, punkOuter))
       )
     )
  {
    if (! IDirectInput8_CreateDevice_Original)
    {
      void** vftable = *(void***)*ppvOut;
      
      SK_CreateFuncHook ( L"IDirectInput8::CreateDevice",
                           vftable [3],
                           IDirectInput8_CreateDevice_Detour,
                 (LPVOID*)&IDirectInput8_CreateDevice_Original );
      
      SK_EnableHook (vftable [3]);
    }
  }

  return hr;
}

void
SK_Input_HookDI8 (void)
{
  if (! config.input.gamepad.hook_dinput8)
    return;

  static volatile LONG hooked = FALSE;

  if (! InterlockedExchangeAdd (&hooked, 0))
  {
    SK_LOG0 ( ( L"Game uses DirectInput, installing input hooks..." ),
                  L"   Input  " );

    SK_CreateDLLHook ( L"dinput8.dll",
                        "DirectInput8Create",
                        DirectInput8Create_Detour,
             (LPVOID *)&DirectInput8Create_Original );

    InterlockedIncrement (&hooked);
  }
}

void
SK_Input_PreHookDI8 (void)
{
  if (! config.input.gamepad.hook_dinput8)
    return;

  if (DirectInput8Create_Original == nullptr)
  {
    static sk_import_test_s tests [] = { { "dinput.dll",  false },
                                         { "dinput8.dll", false } };

    SK_TestImports (GetModuleHandle (nullptr), tests, 2);

    if (tests [0].used || tests [1].used)// || GetModuleHandle (L"dinput8.dll"))
    {
      SK_Input_HookDI8 ();
    }
  }
}


/////////////////////////////////////////////////
//
// ImGui Cursor Management
//
/////////////////////////////////////////////////
#include <imgui/imgui.h>
sk_imgui_cursor_s SK_ImGui_Cursor;

HCURSOR GetGameCursor (void);


void
sk_imgui_cursor_s::update (void)
{
  if (GetGameCursor () != nullptr)
    SK_ImGui_Cursor.orig_img = GetGameCursor ();

  if (SK_ImGui_Visible)
  {
    if (ImGui::GetIO ().WantCaptureMouse || SK_ImGui_Cursor.orig_img == nullptr)
      SK_ImGui_Cursor.showSystemCursor (false);
    else
      SK_ImGui_Cursor.showSystemCursor (true);
  }

  else
    SK_ImGui_Cursor.showSystemCursor ();
}

void
sk_imgui_cursor_s::showImGuiCursor (void)
{
  showSystemCursor (false);
}

void
sk_imgui_cursor_s::LocalToScreen (LPPOINT lpPoint)
{
  LocalToClient  (lpPoint);
  ClientToScreen (game_window.hWnd, lpPoint);
}

void
sk_imgui_cursor_s::LocalToClient (LPPOINT lpPoint)
{
  RECT real_client;
  GetClientRect (game_window.hWnd, &real_client);

  ImVec2 local_dims =
    ImGui::GetIO ().DisplayFramebufferScale;

  struct {
    float width  = 1.0f,
          height = 1.0f;
  } in, out;

  in.width   = local_dims.x;
  in.height  = local_dims.y;

  out.width  = (float)(real_client.right  - real_client.left);
  out.height = (float)(real_client.bottom - real_client.top);

  float x = 2.0f * ((float)lpPoint->x / std::max (1.0f, in.width )) - 1.0f;
  float y = 2.0f * ((float)lpPoint->y / std::max (1.0f, in.height)) - 1.0f;

  lpPoint->x = (LONG)( ( x * out.width  + out.width  ) / 2.0f );
  lpPoint->y = (LONG)( ( y * out.height + out.height ) / 2.0f );
}

void
sk_imgui_cursor_s::ClientToLocal    (LPPOINT lpPoint)
{
  RECT real_client;
  GetClientRect (game_window.hWnd, &real_client);

  const ImVec2 local_dims =
    ImGui::GetIO ().DisplayFramebufferScale;

  struct {
    float width  = 1.0f,
          height = 1.0f;
  } in, out;

  out.width  = local_dims.x;
  out.height = local_dims.y;

  in.width   = (float)(real_client.right  - real_client.left);
  in.height  = (float)(real_client.bottom - real_client.top);

  float x = 2.0f * ((float)lpPoint->x / std::max (1.0f, in.width )) - 1.0f;
  float y = 2.0f * ((float)lpPoint->y / std::max (1.0f, in.height)) - 1.0f;
                                        // Avoid division-by-zero, this should be a signaling NAN but
                                        //   some games alter FPU behavior and will turn this into a non-continuable exception.

  lpPoint->x = (LONG)( ( x * out.width  + out.width  ) / 2.0f );
  lpPoint->y = (LONG)( ( y * out.height + out.height ) / 2.0f );
}

void
sk_imgui_cursor_s::ScreenToLocal (LPPOINT lpPoint)
{
  ScreenToClient (game_window.hWnd, lpPoint);
  ClientToLocal  (lpPoint);
}


#include <resource.h>

HCURSOR
ImGui_DesiredCursor (void)
{
  static HCURSOR last_cursor = 0;

  if (ImGui::GetIO ().MouseDownDuration [0] <= 0.0f || last_cursor == 0)
  {
    switch (ImGui::GetMouseCursor ())
    {
      case ImGuiMouseCursor_Arrow:
        //SetCursor_Original ((last_cursor = LoadCursor (nullptr, IDC_ARROW)));
        return ((last_cursor = LoadCursor (SK_GetDLL (), (LPCWSTR)IDC_CURSOR_POINTER)));
        break;                          
      case ImGuiMouseCursor_TextInput:  
        return ((last_cursor = LoadCursor (nullptr, IDC_IBEAM)));
        break;                          
      case ImGuiMouseCursor_ResizeEW:
        //SetCursor_Original ((last_cursor = LoadCursor (nullptr, IDC_SIZEWE)));
        return ((last_cursor = LoadCursor (SK_GetDLL (), (LPCWSTR)IDC_CURSOR_HORZ)));
        break;                          
      case ImGuiMouseCursor_ResizeNWSE: 
        return ((last_cursor = LoadCursor (nullptr, IDC_SIZENWSE)));
        break;
    }
  }

  else
    return (last_cursor);

  return GetCursor ();
}

void
ImGuiCursor_Impl (void)
{
  CURSORINFO ci;
  ci.cbSize = sizeof CURSORINFO;

  GetCursorInfo_Original (&ci);

  //
  // Hardware Cursor
  //
  if (config.input.ui.use_hw_cursor)
    SetCursor_Original (ImGui_DesiredCursor ());

  if ( config.input.ui.use_hw_cursor && (ci.flags & CURSOR_SHOWING) )
  {
    ImGui::GetIO ().MouseDrawCursor = false;
  }
  
  //
  // Software
  //
  else
  {
    if (SK_ImGui_Visible)
    {
      SetCursor_Original (nullptr);
      ImGui::GetIO ().MouseDrawCursor = (! SK_ImGui_Cursor.idle);
    }
  }
}

void
sk_imgui_cursor_s::showSystemCursor (bool system)
{
  CURSORINFO cursor_info;
  cursor_info.cbSize = sizeof (CURSORINFO);

  static HCURSOR wait_cursor = LoadCursor (nullptr, IDC_WAIT);

  if (SK_ImGui_Cursor.orig_img == wait_cursor)
    SK_ImGui_Cursor.orig_img = LoadCursor (nullptr, IDC_ARROW);

  if (system)
  {
    SetCursor_Original     (SK_ImGui_Cursor.orig_img);
    GetCursorInfo_Original (&cursor_info);

    if ((! SK_ImGui_Visible) || (cursor_info.flags & CURSOR_SHOWING))
      ImGui::GetIO ().MouseDrawCursor = false;

    else
      ImGuiCursor_Impl ();
  }

  else
    ImGuiCursor_Impl ();
}


void
sk_imgui_cursor_s::activateWindow (bool active)
{
  CURSORINFO ci;
  ci.cbSize = sizeof ci;
  
  GetCursorInfo_Original (&ci);
  
  if (active)
  {
    if (SK_ImGui_Visible)
    {
      if (SK_ImGui_WantMouseCapture ())
      {
  
      } else if (SK_ImGui_Cursor.orig_img)
        SetCursor_Original (SK_ImGui_Cursor.orig_img);
    }
  }
}




HCURSOR game_cursor    = 0;

bool
SK_ImGui_WantKeyboardCapture (void)
{
  bool imgui_capture = false;

  if (SK_ImGui_Visible)
  {
    ImGuiIO& io =
      ImGui::GetIO ();

    if (nav_usable || io.WantCaptureKeyboard || io.WantTextInput)
      imgui_capture = true;
  }

  return imgui_capture;
}

bool
SK_ImGui_WantTextCapture (void)
{
  bool imgui_capture = false;

  if (SK_ImGui_Visible)
  {
    ImGuiIO& io =
      ImGui::GetIO ();

    if (io.WantTextInput)
      imgui_capture = true;
  }

  return imgui_capture;
}

bool
SK_ImGui_WantGamepadCapture (void)
{
  bool imgui_capture = false;

  if (SK_ImGui_Visible)
  {
    if (nav_usable)
      imgui_capture = true;
  }

  // Stupid hack, breaking whatever abstraction this horrible mess passes for
  extern bool __FAR_Freelook;
  if (__FAR_Freelook)
    imgui_capture = true;

  return imgui_capture;
}

bool
SK_ImGui_WantMouseCapture (void)
{
  bool imgui_capture = false;

  if (SK_ImGui_Visible)
  {
    ImGuiIO& io =
      ImGui::GetIO ();

    if (config.input.ui.capture_mouse || io.WantCaptureMouse/* || io.WantTextInput*/)
      imgui_capture = true;

    if (config.input.ui.capture_hidden && (! SK_InputUtil_IsHWCursorVisible ()))
      imgui_capture = true;
  }

  return imgui_capture;
}

HCURSOR GetGameCursor (void)
{
  static HCURSOR sk_imgui_arrow = LoadCursor (SK_GetDLL (), (LPCWSTR)IDC_CURSOR_POINTER);
  static HCURSOR sk_imgui_horz  = LoadCursor (SK_GetDLL (), (LPCWSTR)IDC_CURSOR_HORZ);
  static HCURSOR sk_imgui_ibeam = LoadCursor (nullptr, IDC_IBEAM);
  static HCURSOR sys_arrow      = LoadCursor (nullptr, IDC_ARROW);
  static HCURSOR sys_wait       = LoadCursor (nullptr, IDC_WAIT);

  static HCURSOR hCurLast = 0;
         HCURSOR hCur     = GetCursor ();

  if ( hCur != sk_imgui_horz && hCur != sk_imgui_arrow && hCur != sk_imgui_ibeam &&
       hCur != sys_arrow     && hCur != sys_wait )
    hCurLast = hCur;

  return hCurLast;
}

void
ImGui_ToggleCursor (void)
{
  if (! SK_ImGui_Cursor.visible)
  {
    if (SK_ImGui_Cursor.orig_img == nullptr)
      SK_ImGui_Cursor.orig_img = GetGameCursor ();

    //GetClipCursor         (&SK_ImGui_Cursor.clip_rect);

    SK_ImGui_CenterCursorOnWindow ();

    // Save original cursor position
    GetCursorPos_Original         (&SK_ImGui_Cursor.pos);
    SK_ImGui_Cursor.ScreenToLocal (&SK_ImGui_Cursor.pos);

    ImGui::GetIO ().WantCaptureMouse = true;
  }

  else
  {
    if (SK_ImGui_Cursor.orig_img == nullptr)
      SK_ImGui_Cursor.orig_img = GetGameCursor ();

    if (SK_ImGui_WantMouseCapture ())
    {
      //ClipCursor_Original   (&SK_ImGui_Cursor.clip_rect);

      POINT screen = SK_ImGui_Cursor.orig_pos;
      SK_ImGui_Cursor.LocalToScreen (&screen);
      SetCursorPos_Original ( screen.x,
                              screen.y );
    }

    ImGui::GetIO ().WantCaptureMouse = false;
  }

  SK_ImGui_Cursor.visible = (! SK_ImGui_Cursor.visible);
  SK_ImGui_Cursor.update ();
}



typedef int (WINAPI *GetMouseMovePointsEx_pfn)(
  _In_  UINT             cbSize,
  _In_  LPMOUSEMOVEPOINT lppt,
  _Out_ LPMOUSEMOVEPOINT lpptBuf,
  _In_  int              nBufPoints,
  _In_  DWORD            resolution
);

GetMouseMovePointsEx_pfn GetMouseMovePointsEx_Original = nullptr;

int
WINAPI
GetMouseMovePointsEx_Detour(
  _In_  UINT             cbSize,
  _In_  LPMOUSEMOVEPOINT lppt,
  _Out_ LPMOUSEMOVEPOINT lpptBuf,
  _In_  int              nBufPoints,
  _In_  DWORD            resolution )
{
  SK_LOG_FIRST_CALL

  if (SK_ImGui_Visible)
  {
    bool implicit_capture = false;

    // Depending on warp prefs, we may not allow the game to know about mouse movement
    //   (even if ImGui doesn't want mouse capture)
    if ( ( SK_ImGui_Cursor.prefs.no_warp.ui_open && SK_ImGui_Visible                  ) ||
         ( SK_ImGui_Cursor.prefs.no_warp.visible && SK_InputUtil_IsHWCursorVisible () )    )
      implicit_capture = true;

    if (SK_ImGui_WantMouseCapture () || implicit_capture)
    {
      return 0;
    }
  }

  return GetMouseMovePointsEx_Original (cbSize, lppt, lpptBuf, nBufPoints, resolution);
}



HCURSOR
WINAPI
SetCursor_Detour (
  _In_opt_ HCURSOR hCursor )
{
  SK_LOG_FIRST_CALL

  SK_ImGui_Cursor.orig_img = hCursor;

  if (! SK_ImGui_Visible)
    return SetCursor_Original (hCursor);
  else if (! (ImGui::GetIO ().WantCaptureMouse || hCursor == nullptr))
    return SetCursor_Original (hCursor);

  return GetGameCursor ();
}

BOOL
WINAPI
GetCursorInfo_Detour (PCURSORINFO pci)
{
  SK_LOG_FIRST_CALL

  POINT pt  = pci->ptScreenPos;
  BOOL  ret = GetCursorInfo_Original (pci);
        pci->ptScreenPos = pt;

  pci->hCursor = SK_ImGui_Cursor.orig_img;


  if (SK_ImGui_Visible)
  {
    bool implicit_capture = false;

    // Depending on warp prefs, we may not allow the game to know about mouse movement
    //   (even if ImGui doesn't want mouse capture)
    if ( ( SK_ImGui_Cursor.prefs.no_warp.ui_open && SK_ImGui_Visible                  ) ||
         ( SK_ImGui_Cursor.prefs.no_warp.visible && SK_InputUtil_IsHWCursorVisible () )    )
      implicit_capture = true;

    if (SK_ImGui_WantMouseCapture () || implicit_capture)
    {
      POINT client = SK_ImGui_Cursor.orig_pos;

      SK_ImGui_Cursor.LocalToScreen (&client);
      pci->ptScreenPos.x = client.x;
      pci->ptScreenPos.y = client.y;
    }

    else {
      POINT client = SK_ImGui_Cursor.pos;

      SK_ImGui_Cursor.LocalToScreen (&client);
      pci->ptScreenPos.x = client.x;
      pci->ptScreenPos.y = client.y;
    }

    return TRUE;
  }


  return GetCursorInfo_Original (pci);
}

BOOL
WINAPI
GetCursorPos_Detour (LPPOINT lpPoint)
{
  SK_LOG_FIRST_CALL


  if (SK_ImGui_Visible)
  {
    bool implicit_capture = false;

    // Depending on warp prefs, we may not allow the game to know about mouse movement
    //   (even if ImGui doesn't want mouse capture)
    if ( ( SK_ImGui_Cursor.prefs.no_warp.ui_open && SK_ImGui_Visible                  ) ||
         ( SK_ImGui_Cursor.prefs.no_warp.visible && SK_InputUtil_IsHWCursorVisible () )    )
      implicit_capture = true;

    if (SK_ImGui_WantMouseCapture () || implicit_capture)
    {
      POINT client = SK_ImGui_Cursor.orig_pos;

      SK_ImGui_Cursor.LocalToScreen (&client);
      lpPoint->x = client.x;
      lpPoint->y = client.y;
    }

    else {
      POINT client = SK_ImGui_Cursor.pos;

      SK_ImGui_Cursor.LocalToScreen (&client);
      lpPoint->x = client.x;
      lpPoint->y = client.y;
    }

    return TRUE;
  }


  return GetCursorPos_Original (lpPoint);
}

BOOL
WINAPI
SetCursorPos_Detour (_In_ int x, _In_ int y)
{
  SK_LOG_FIRST_CALL

  // Game WANTED to change its position, so remember that.
  SK_ImGui_Cursor.orig_pos.x = x;
  SK_ImGui_Cursor.orig_pos.y = y;

  SK_ImGui_Cursor.ScreenToLocal (&SK_ImGui_Cursor.orig_pos);

  // Don't let the game continue moving the cursor while
  //   Alt+Tabbed out
  if ((! SK_GetCurrentRenderBackend ().fullscreen_exclusive) &&
      config.window.background_render && (! game_window.active))
    return TRUE;

  // Prevent Mouse Look while Drag Locked
  if (config.window.drag_lock)
    return TRUE;

  if ( ( SK_ImGui_Cursor.prefs.no_warp.ui_open && SK_ImGui_Visible                  ) ||
       ( SK_ImGui_Cursor.prefs.no_warp.visible && SK_InputUtil_IsHWCursorVisible () )    )
  {
    //game_mouselook = SK_GetFramesDrawn ();
  }

  else if (! SK_ImGui_WantMouseCapture ())
  {
    return SetCursorPos_Original (x, y);
  }

  return TRUE;
}

UINT
WINAPI
SendInput_Detour (
  _In_ UINT    nInputs,
  _In_ LPINPUT pInputs,
  _In_ int     cbSize
)
{
  SK_LOG_FIRST_CALL

  // TODO: Process this the right way...

  if (SK_ImGui_Visible)
  {
    return 0;
  }

  return SendInput_Original (nInputs, pInputs, cbSize);
}

keybd_event_pfn keybd_event_Original = nullptr;

void
WINAPI
keybd_event_Detour (
    _In_ BYTE bVk,
    _In_ BYTE bScan,
    _In_ DWORD dwFlags,
    _In_ ULONG_PTR dwExtraInfo
)
{
  SK_LOG_FIRST_CALL

// TODO: Process this the right way...

  if (SK_ImGui_Visible)
  {
    return;
  }

  keybd_event_Original (bVk, bScan, dwFlags, dwExtraInfo);
}

VOID
WINAPI
mouse_event_Detour (
  _In_ DWORD     dwFlags,
  _In_ DWORD     dx,
  _In_ DWORD     dy,
  _In_ DWORD     dwData,
  _In_ ULONG_PTR dwExtraInfo
)
{
  SK_LOG_FIRST_CALL

// TODO: Process this the right way...

  if (SK_ImGui_Visible)
  {
    return;
  }

  mouse_event_Original (dwFlags, dx, dy, dwData, dwExtraInfo);
}


GetKeyState_pfn             GetKeyState_Original             = nullptr;
GetAsyncKeyState_pfn        GetAsyncKeyState_Original        = nullptr;
GetKeyboardState_pfn        GetKeyboardState_Original        = nullptr;

SHORT
WINAPI
GetAsyncKeyState_Detour (_In_ int vKey)
{
  SK_LOG_FIRST_CALL

#define SK_ConsumeVKey(vKey) { GetAsyncKeyState_Original(vKey); return 0; }

  // Block keyboard input to the game while the console is active
  if (SK_Console::getInstance ()->isVisible ())
    SK_ConsumeVKey (vKey);

  // Block keyboard input to the game while it's in the background
  if ((! SK_GetCurrentRenderBackend ().fullscreen_exclusive) &&
      config.window.background_render && (! game_window.active))
    SK_ConsumeVKey (vKey);

  if ((vKey & 0xFF) >= 5)
  {
    if (SK_ImGui_WantKeyboardCapture ())
      SK_ConsumeVKey (vKey);
  }

  else
  {
    // Some games use this API for mouse buttons, for reasons that are beyond me...
    if (SK_ImGui_WantMouseCapture ())
      SK_ConsumeVKey (vKey);
  }

  return GetAsyncKeyState_Original (vKey);
}

SHORT
WINAPI
GetKeyState_Detour (_In_ int nVirtKey)
{
  SK_LOG_FIRST_CALL

#define SK_ConsumeVirtKey(nVirtKey) { GetKeyState_Original(nVirtKey); return 0; }

  // Block keyboard input to the game while the console is active
  if (SK_Console::getInstance ()->isVisible ())
    SK_ConsumeVirtKey (nVirtKey);

  // Block keyboard input to the game while it's in the background
  if ((! SK_GetCurrentRenderBackend ().fullscreen_exclusive) &&
      config.window.background_render && (! game_window.active))
    SK_ConsumeVirtKey (nVirtKey);

  if ((nVirtKey & 0xFF) >= 5)
  {
    if (SK_ImGui_WantKeyboardCapture ())
      SK_ConsumeVirtKey (nVirtKey);
  }

  else
  {
    // Some games use this API for mouse buttons, for reasons that are beyond me...
    if (SK_ImGui_WantMouseCapture ())
      SK_ConsumeVirtKey (nVirtKey);
  }

  return GetKeyState_Original (nVirtKey);
}

//typedef BOOL (WINAPI *SetKeyboardState_pfn)(PBYTE lpKeyState); // TODO

BOOL
WINAPI
GetKeyboardState_Detour (PBYTE lpKeyState)
{
  SK_LOG_FIRST_CALL

  if (SK_ImGui_WantKeyboardCapture ())
  {
    memset (lpKeyState, 0, 256);
    return TRUE;
  }

  BOOL bRet = GetKeyboardState_Original (lpKeyState);

  if (bRet)
  {
    // Some games use this API for mouse buttons, for reasons that are beyond me...
    if (SK_ImGui_WantMouseCapture ())
      memset (lpKeyState, 0, 5);
  }

  return bRet;
}

#include <Windowsx.h>
#include <dbt.h>

LRESULT
WINAPI
ImGui_WndProcHandler (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool
SK_ImGui_HandlesMessage (LPMSG lpMsg, bool remove, bool peek)
{
  bool handled = false;

  switch (lpMsg->message)
  {
    case WM_SYSCOMMAND:
      if ((lpMsg->wParam & 0xfff0) == SC_KEYMENU && lpMsg->lParam == 0) // Disable ALT application menu
        handled = true;
      break;

    case WM_CHAR:
      handled = SK_ImGui_WantTextCapture ();
      break;

    // Fix for Melody's Escape, which attempts to remove these messages!
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    {
      if (peek)
      {
        if (lpMsg->message == WM_KEYDOWN || lpMsg->message == WM_SYSKEYDOWN)
          if (! SK_Console::getInstance ()->KeyDown (lpMsg->wParam & 0xFF, lpMsg->lParam))
            handled = true;

        if (lpMsg->message == WM_KEYUP || lpMsg->message == WM_SYSKEYUP)
          if (! SK_Console::getInstance ()->KeyUp (lpMsg->wParam & 0xFF, lpMsg->lParam))
            handled = true;
      }

      if (( (! peek) && ImGui_WndProcHandler (lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam) ) || SK_Console::getInstance ()->isVisible ())
        handled = true;
    } break;

    case WM_SETCURSOR:
    {
      if (lpMsg->hwnd == game_window.hWnd && game_window.hWnd != 0)
        SK_ImGui_Cursor.update ();
    } break;

    // TODO: Does this message have an HWND always?
    case WM_DEVICECHANGE:
    {
      handled = ImGui_WndProcHandler (lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    } break;

    // Pre-Dispose These Mesages (fixes The Witness)
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:

    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    {
      handled = true;

      if (peek)
      {
        if (SK_ImGui_WantMouseCapture ())
          ImGui_WndProcHandler (lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
      }

      else
        ImGui_WndProcHandler (lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);

      if (! SK_ImGui_WantMouseCapture ())
        handled = false;
    } break;

    case WM_INPUT:
    {
      if (ImGui_WndProcHandler (lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam) || SK_Console::getInstance ()->isVisible ())
        handled = true;
    } break;

    default:
    {
      ImGui_WndProcHandler (lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    } break;
  }

  return handled;
}


#include <SpecialK/input/xinput_hotplug.h>

void SK_Input_Init (void);


// Parts of the Win32 API that are safe to hook from DLL Main
void SK_Input_PreInit (void)
{
  SK_CreateDLLHook2 ( L"user32.dll", "GetRawInputData",
                     GetRawInputData_Detour,
           (LPVOID*)&GetRawInputData_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "GetAsyncKeyState",
                     GetAsyncKeyState_Detour,
           (LPVOID*)&GetAsyncKeyState_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "GetKeyState",
                     GetKeyState_Detour,
           (LPVOID*)&GetKeyState_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "GetKeyboardState",
                     GetKeyboardState_Detour,
           (LPVOID*)&GetKeyboardState_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "GetCursorPos",
                     GetCursorPos_Detour,
           (LPVOID*)&GetCursorPos_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "GetCursorInfo",
                     GetCursorInfo_Detour,
           (LPVOID*)&GetCursorInfo_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "GetMouseMovePointsEx",
                     GetMouseMovePointsEx_Detour,
           (LPVOID*)&GetMouseMovePointsEx_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "SetCursor",
                     SetCursor_Detour,
           (LPVOID*)&SetCursor_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "SetCursorPos",
                     SetCursorPos_Detour,
           (LPVOID*)&SetCursorPos_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "SendInput",
                     SendInput_Detour,
           (LPVOID*)&SendInput_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "mouse_event",
                     mouse_event_Detour,
           (LPVOID*)&mouse_event_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "keybd_event",
                     keybd_event_Detour,
           (LPVOID*)&keybd_event_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "RegisterRawInputDevices",
                     RegisterRawInputDevices_Detour,
           (LPVOID*)&RegisterRawInputDevices_Original );

  SK_CreateDLLHook2 ( L"user32.dll", "GetRegisteredRawInputDevices",
                      GetRegisteredRawInputDevices_Detour,
           (LPVOID *)&GetRegisteredRawInputDevices_Original );

#if 0
  SK_CreateDLLHook2 ( L"user32.dll", "GetRawInputBuffer",
                     GetRawInputBuffer_Detour,
           (LPVOID*)&GetRawInputBuffer_Original );
#endif

  if (config.input.gamepad.hook_xinput)
    SK_XInput_InitHotPlugHooks ();

  MH_ApplyQueued ();

  SK_Input_Init ();
}


void
SK_Input_Init (void)
{
  SK_Input_PreHookHID    ();
  SK_Input_PreHookDI8    ();
  SK_Input_PreHookXInput ();

  SK_ApplyQueuedHooks    ();
}



sk_input_api_context_s SK_XInput_Backend;
sk_input_api_context_s SK_DI8_Backend;
sk_input_api_context_s SK_HID_Backend;
sk_input_api_context_s SK_RawInput_Backend;