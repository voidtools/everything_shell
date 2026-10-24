//
// Copyright (C) 2026 voidtools / David Carpenter
// 
// Permission is hereby granted, free of charge, 
// to any person obtaining a copy of this software 
// and associated documentation files (the "Software"), 
// to deal in the Software without restriction, 
// including without limitation the rights to use, 
// copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit 
// persons to whom the Software is furnished to do so, 
// subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be 
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

// EverythingShell
// Windows 11 modern context menu item: "Search Everything..."
// Launches Everything.exe -path <folder>
//
// Built without the CRT (see the project): no memcpy/memset/memcmp/wcslen/printf.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <ocidl.h>
#include <initguid.h>
#include "resource.h"

// {6F868C19-C17D-4AD4-BF42-75354AFD2BA5}
// must match the Clsid in Package\AppxManifest.xml
DEFINE_GUID(CLSID_EverythingShellCommand, 0x6f868c19, 0xc17d, 0x4ad4, 0xbf, 0x42, 0x75, 0x35, 0x4a, 0xfd, 0x2b, 0xa5);

#define EVERYTHING_REG_KEY      L"SOFTWARE\\voidtools\\Everything"
#define EVERYTHING_REG_VALUE    L"ExePath"
#define EVERYTHING_DEFAULT_EXE  L"C:\\Program Files\\Everything\\Everything.exe"

static HINSTANCE g_hinst;
static LONG g_objects;

static BOOL GuidEq(const GUID *a, const GUID *b)
{
	const DWORD *x = (const DWORD *)a;
	const DWORD *y = (const DWORD *)b;

	return x[0] == y[0] && x[1] == y[1] && x[2] == y[2] && x[3] == y[3];
}

typedef struct CMD_S
{
	IExplorerCommand command;	// must be first
	IObjectWithSite site;
	LONG refs;
	IUnknown *punksite;
}CMD;

#define CMD_FROM_SITE(p) CONTAINING_RECORD(p, CMD, site)

// ---------------------------------------------------------------- launch

// folder == NULL: just open Everything with an empty search.
static HRESULT LaunchEverything(PCWSTR folder)
{
	WCHAR exe[MAX_PATH];
	DWORD cb;
	PWSTR params = NULL;
	SHELLEXECUTEINFOW sei;
	HRESULT hr;

	cb = sizeof(exe);
	if (RegGetValueW(HKEY_LOCAL_MACHINE, EVERYTHING_REG_KEY, EVERYTHING_REG_VALUE, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, exe, &cb) != ERROR_SUCCESS || !exe[0])
	{
		lstrcpynW(exe, EVERYTHING_DEFAULT_EXE, ARRAYSIZE(exe));
	}

	if (folder)
	{
		size_t cch;

		cch = (size_t)lstrlenW(folder) + 16;
		params = (PWSTR)HeapAlloc(GetProcessHeap(), 0, cch * sizeof(WCHAR));
		if (!params)
		{
			return E_OUTOFMEMORY;
		}

		// Everything parses the command with its own syntax, don't escape trailing '\\'
		wnsprintfW(params, (int)cch, L"-path \"%s\"", folder);
	}

	SecureZeroMemory(&sei, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOASYNC;
	sei.lpFile = exe;
	sei.lpParameters = params ? params : L"-search \"\"";
	sei.nShow = SW_SHOWNORMAL;

	hr = ShellExecuteExW(&sei) ? S_OK : HRESULT_FROM_WIN32(GetLastError());

	if (params) HeapFree(GetProcessHeap(), 0, params);

	return hr;
}

// ---------------------------------------------------------------- folder lookup

// folder item that was right clicked.
static HRESULT FolderFromItems(IShellItemArray *psia, PWSTR *path)
{
	IShellItem *psi;
	HRESULT hr;

	hr = IShellItemArray_GetItemAt(psia, 0, &psi);
	if (SUCCEEDED(hr))
	{
		hr = IShellItem_GetDisplayName(psi, SIGDN_FILESYSPATH, path);
		IShellItem_Release(psi);
	}

	return hr;
}

// folder background right click; the folder is the current folder of the browser.
static HRESULT FolderFromSite(IUnknown *punksite, PWSTR *path)
{
	IServiceProvider *psp = NULL;
	IShellBrowser *psb = NULL;
	IShellView *psv = NULL;
	IFolderView *pfv = NULL;
	IPersistFolder2 *ppf = NULL;
	PIDLIST_ABSOLUTE pidl = NULL;
	HRESULT hr;

	hr = IUnknown_QueryInterface(punksite, &IID_IServiceProvider, (void **)&psp);
	if (SUCCEEDED(hr)) hr = IServiceProvider_QueryService(psp, &SID_STopLevelBrowser, &IID_IShellBrowser, (void **)&psb);
	if (SUCCEEDED(hr)) hr = IShellBrowser_QueryActiveShellView(psb, &psv);
	if (SUCCEEDED(hr)) hr = IShellView_QueryInterface(psv, &IID_IFolderView, (void **)&pfv);
	if (SUCCEEDED(hr)) hr = IFolderView_GetFolder(pfv, &IID_IPersistFolder2, (void **)&ppf);
	if (SUCCEEDED(hr)) hr = IPersistFolder2_GetCurFolder(ppf, &pidl);
	if (SUCCEEDED(hr)) hr = SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, path);

	if (pidl) CoTaskMemFree(pidl);
	if (ppf) IPersistFolder2_Release(ppf);
	if (pfv) IFolderView_Release(pfv);
	if (psv) IShellView_Release(psv);
	if (psb) IShellBrowser_Release(psb);
	if (psp) IServiceProvider_Release(psp);

	return hr;
}

// ---------------------------------------------------------------- CMD object

static ULONG CmdAddRef(CMD *c)
{
	return (ULONG)InterlockedIncrement(&c->refs);
}

static ULONG CmdRelease(CMD *c)
{
	LONG refs;

	refs = InterlockedDecrement(&c->refs);
	if (!refs)
	{
		if (c->punksite) IUnknown_Release(c->punksite);
		HeapFree(GetProcessHeap(), 0, c);
		InterlockedDecrement(&g_objects);
	}

	return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE cmd_QueryInterface(IExplorerCommand *This, REFIID riid, void **ppv)
{
	CMD *c = (CMD *)This;

	if (!ppv) return E_POINTER;

	if (GuidEq(riid, &IID_IUnknown) || GuidEq(riid, &IID_IExplorerCommand))
	{
		*ppv = &c->command;
	}
	else if (GuidEq(riid, &IID_IObjectWithSite))
	{
		*ppv = &c->site;
	}
	else
	{
		*ppv = NULL;
		return E_NOINTERFACE;
	}

	CmdAddRef(c);
	return S_OK;
}

static ULONG STDMETHODCALLTYPE cmd_AddRef(IExplorerCommand *This)
{
	return CmdAddRef((CMD *)This);
}

static ULONG STDMETHODCALLTYPE cmd_Release(IExplorerCommand *This)
{
	return CmdRelease((CMD *)This);
}

static HRESULT STDMETHODCALLTYPE cmd_GetTitle(IExplorerCommand *This, IShellItemArray *psia, LPWSTR *ppszName)
{
	PCWSTR p = NULL;
	int n;
	PWSTR s;

	(void)This; (void)psia;

	*ppszName = NULL;

	// cchBufferMax == 0: p points at the (non null terminated) string inside the resource.
	// the string is chosen from the current UI language.
	n = LoadStringW(g_hinst, IDS_SEARCH_EVERYTHING, (LPWSTR)&p, 0);
	if (n <= 0) return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);

	s = (PWSTR)CoTaskMemAlloc(((size_t)n + 1) * sizeof(WCHAR));
	if (!s) return E_OUTOFMEMORY;

	lstrcpynW(s, p, n + 1);	// copies n chars and null terminates
	*ppszName = s;

	return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmd_GetIcon(IExplorerCommand *This, IShellItemArray *psia, LPWSTR *ppszIcon)
{
	WCHAR path[MAX_PATH + 16];
	DWORD n;

	(void)This; (void)psia;

	*ppszIcon = NULL;

	n = GetModuleFileNameW(g_hinst, path, MAX_PATH);
	if (!n || n >= MAX_PATH) return E_FAIL;

	// "<dll>,-<resource id>"
	wnsprintfW(path + n, (int)(ARRAYSIZE(path) - n), L",-%d", IDI_EVERYTHING);

	return SHStrDupW(path, ppszIcon);
}

static HRESULT STDMETHODCALLTYPE cmd_GetToolTip(IExplorerCommand *This, IShellItemArray *psia, LPWSTR *ppszInfo)
{
	(void)This; (void)psia;
	*ppszInfo = NULL;
	return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE cmd_GetCanonicalName(IExplorerCommand *This, GUID *pguidCommandName)
{
	(void)This;
	*pguidCommandName = CLSID_EverythingShellCommand;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmd_GetState(IExplorerCommand *This, IShellItemArray *psia, BOOL fOkToBeSlow, EXPCMDSTATE *pCmdState)
{
	(void)This; (void)psia; (void)fOkToBeSlow;
	*pCmdState = ECS_ENABLED;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmd_Invoke(IExplorerCommand *This, IShellItemArray *psia, IBindCtx *pbc)
{
	CMD *c = (CMD *)This;
	PWSTR folder = NULL;
	HRESULT hr = E_FAIL;

	(void)pbc;

	if (psia) hr = FolderFromItems(psia, &folder);
	if (FAILED(hr) && c->punksite) hr = FolderFromSite(c->punksite, &folder);

	if (SUCCEEDED(hr))
	{
		hr = LaunchEverything(folder);
		CoTaskMemFree(folder);
	}
	else
	{
		// no file system folder (eg: This PC), just open Everything.
		hr = LaunchEverything(NULL);
	}

	return hr;
}

static HRESULT STDMETHODCALLTYPE cmd_GetFlags(IExplorerCommand *This, EXPCMDFLAGS *pFlags)
{
	(void)This;
	*pFlags = ECF_DEFAULT;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmd_EnumSubCommands(IExplorerCommand *This, IEnumExplorerCommand **ppEnum)
{
	(void)This;
	*ppEnum = NULL;
	return E_NOTIMPL;
}

static IExplorerCommandVtbl g_cmd_vtbl =
{
	cmd_QueryInterface,
	cmd_AddRef,
	cmd_Release,
	cmd_GetTitle,
	cmd_GetIcon,
	cmd_GetToolTip,
	cmd_GetCanonicalName,
	cmd_GetState,
	cmd_Invoke,
	cmd_GetFlags,
	cmd_EnumSubCommands,
};

// ---------------------------------------------------------------- IObjectWithSite

static HRESULT STDMETHODCALLTYPE site_QueryInterface(IObjectWithSite *This, REFIID riid, void **ppv)
{
	return cmd_QueryInterface(&CMD_FROM_SITE(This)->command, riid, ppv);
}

static ULONG STDMETHODCALLTYPE site_AddRef(IObjectWithSite *This)
{
	return CmdAddRef(CMD_FROM_SITE(This));
}

static ULONG STDMETHODCALLTYPE site_Release(IObjectWithSite *This)
{
	return CmdRelease(CMD_FROM_SITE(This));
}

static HRESULT STDMETHODCALLTYPE site_SetSite(IObjectWithSite *This, IUnknown *punkSite)
{
	CMD *c = CMD_FROM_SITE(This);

	if (c->punksite) IUnknown_Release(c->punksite);
	c->punksite = punkSite;
	if (punkSite) IUnknown_AddRef(punkSite);

	return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_GetSite(IObjectWithSite *This, REFIID riid, void **ppv)
{
	CMD *c = CMD_FROM_SITE(This);

	*ppv = NULL;
	if (!c->punksite) return E_FAIL;

	return IUnknown_QueryInterface(c->punksite, riid, ppv);
}

static IObjectWithSiteVtbl g_site_vtbl =
{
	site_QueryInterface,
	site_AddRef,
	site_Release,
	site_SetSite,
	site_GetSite,
};

// ---------------------------------------------------------------- class factory

static HRESULT STDMETHODCALLTYPE cf_QueryInterface(IClassFactory *This, REFIID riid, void **ppv)
{
	if (!ppv) return E_POINTER;

	if (GuidEq(riid, &IID_IUnknown) || GuidEq(riid, &IID_IClassFactory))
	{
		*ppv = This;
		return S_OK;
	}

	*ppv = NULL;
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE cf_AddRef(IClassFactory *This)
{
	(void)This;
	return 2;
}

static ULONG STDMETHODCALLTYPE cf_Release(IClassFactory *This)
{
	(void)This;
	return 1;
}

static HRESULT STDMETHODCALLTYPE cf_CreateInstance(IClassFactory *This, IUnknown *punkOuter, REFIID riid, void **ppv)
{
	CMD *c;
	HRESULT hr;

	(void)This;

	if (!ppv) return E_POINTER;
	*ppv = NULL;

	if (punkOuter) return CLASS_E_NOAGGREGATION;

	c = (CMD *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CMD));
	if (!c) return E_OUTOFMEMORY;

	c->command.lpVtbl = &g_cmd_vtbl;
	c->site.lpVtbl = &g_site_vtbl;
	c->refs = 1;
	InterlockedIncrement(&g_objects);

	hr = cmd_QueryInterface(&c->command, riid, ppv);
	CmdRelease(c);

	return hr;
}

static HRESULT STDMETHODCALLTYPE cf_LockServer(IClassFactory *This, BOOL fLock)
{
	(void)This;

	if (fLock) InterlockedIncrement(&g_objects);
	else InterlockedDecrement(&g_objects);

	return S_OK;
}

static IClassFactoryVtbl g_cf_vtbl =
{
	cf_QueryInterface,
	cf_AddRef,
	cf_Release,
	cf_CreateInstance,
	cf_LockServer,
};

static IClassFactory g_cf = { &g_cf_vtbl };

// ---------------------------------------------------------------- exports

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
	if (!ppv) return E_POINTER;
	*ppv = NULL;

	if (!GuidEq(rclsid, &CLSID_EverythingShellCommand)) return CLASS_E_CLASSNOTAVAILABLE;

	return cf_QueryInterface(&g_cf, riid, ppv);
}

STDAPI DllCanUnloadNow(void)
{
	return g_objects ? S_FALSE : S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID reserved)
{
	(void)reserved;

	if (reason == DLL_PROCESS_ATTACH)
	{
		g_hinst = hinstDLL;
		DisableThreadLibraryCalls(hinstDLL);
	}

	return TRUE;
}
