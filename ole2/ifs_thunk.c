
#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "wownt32.h"
#include "ole2.h"
#include "winerror.h"

#include "wine/winbase16.h"
#include "wine/wingdi16.h"
#include "wine/winuser16.h"
#include "ifs.h"

#include "wine/debug.h"
#include "ifs_thunk.h"

extern interface_entry interfaces[];
extern size_t interfaces_count;
/* FIXME */
interface_32 *interface32_instances[1024];
size_t interface32_instance_size = 1024;
size_t interface32_instance_cur = 0;
interface_16 *interface16_instances[1024];
size_t interface16_instance_size = 1024;
size_t interface16_instance_cur = 0;

#ifdef _DEBUG
#define IFS_GUARD_SIZE 500
#else
#define IFS_GUARD_SIZE 0
#endif
static int iid_cmp(const void *p1, const void *p2)
{
    return memcmp(&((const interface_entry*)p1)->iid, &((const interface_entry*)p2)->iid, sizeof(IID));
}
SEGPTR make_thunk_32(void *funcptr, const char *arguments, const char *name, BOOL ret_32bit, BOOL reg_func, BOOL is_cdecl);
static void register_instance32(interface_32 *i32)
{
    if (interface32_instance_cur >= interface32_instance_size)
        return;
    interface32_instances[interface32_instance_cur++] = i32;
}
static void register_instance16(interface_16 *i16)
{
    if (interface16_instance_cur >= interface16_instance_size)
        return;
    interface16_instances[interface16_instance_cur++] = i16;
}
static void init_interface_entry(interface_entry *e)
{
    size_t i = 0;
    SEGPTR *vtbl16 = e->lpVtbl16;
    while (e->vtbl16[i].func16)
    {
        vtbl16[i] = make_thunk_32(e->vtbl16[i].func16, e->vtbl16[i].args, e->vtbl16[i].name, TRUE, FALSE, TRUE);
        i++;
    }
    e->spVtbl16 = MapLS(e->lpVtbl16);
}
SEGPTR iface32_16(REFIID riid, void *iface32)
{
    interface_entry *result;
    size_t i;
    interface_16 *i16;
    SEGPTR s;
    if (!iface32)
    {
        return 0;
    }
    result = (interface_entry*)bsearch(riid, interfaces, interfaces_count, sizeof(interfaces[0]), iid_cmp);
    for (i = 0; i < interface16_instance_size; i++)
    {
        if (interface32_instances[i] && &interface32_instances[i]->lpVtbl == iface32)
        {
            if (!memcmp(interface32_instances[i]->riid, riid, sizeof(IID)))
            {
                s = interface32_instances[i]->iface16;
                TRACE("32-bit interface %p -> %04x:%04x(%.*s)\n", iface32, SELECTOROF(s), OFFSETOF(s), strstr(result->vtbl16[0].name, "::") - result->vtbl16[0].name, result->vtbl16[0].name);
                return s;
            }
        }
        if (interface16_instances[i] && interface16_instances[i]->lpVtbl == iface32)
        {
            if (!memcmp(interface16_instances[i]->riid, riid, sizeof(IID)))
            {
                s = MapLS(&interface16_instances[i]->lpVtbl);
                TRACE("32-bit interface %p -> %04x:%04x(%.*s)\n", iface32, SELECTOROF(s), OFFSETOF(s), strstr(result->vtbl16[0].name, "::") - result->vtbl16[0].name, result->vtbl16[0].name);
                return s;
            }
        }
    }
    if (!result)
    {
        ERR("unknown interface %s\n", debugstr_guid(riid));
        return 0;
    }
    i16 = (interface_16*)HeapAlloc(GetProcessHeap(), 0, sizeof(interface_16) + IFS_GUARD_SIZE * 2);
    memset(i16, 0xcd, sizeof(interface_16) + IFS_GUARD_SIZE * 2);
    i16 = (interface_16*)((char*)i16 + IFS_GUARD_SIZE);
    if (!result->spVtbl16)
    {
        init_interface_entry(result);
    }
    s = MapLS(&i16->lpVtbl);
    TRACE("32-bit interface %p -> new %04x:%04x(%.*s)\n", iface32, SELECTOROF(s), OFFSETOF(s), strstr(result->vtbl16[0].name, "::") - result->vtbl16[0].name, result->vtbl16[0].name);
    i16->iface32 = iface32;
    i16->lpVtbl = result->spVtbl16;
    i16->riid = &result->iid;
    register_instance16(i16);
    return s;
}
void *iface16_32(REFIID riid, SEGPTR iface16)
{
    interface_entry *result;
    size_t i;
    interface_32 *i32;
    if (!iface16)
    {
        return 0;
    }
    result = (interface_entry*)bsearch(riid, interfaces, interfaces_count, sizeof(interfaces[0]), iid_cmp);
    for (i = 0; i < interface32_instance_size; i++)
    {
        if (interface32_instances[i] && interface32_instances[i]->iface16 == iface16)
        {
            if (!memcmp(interface32_instances[i]->riid, riid, sizeof(IID)))
            {
                TRACE("16-bit interface %04x:%04x -> %p(%.*s)\n", SELECTOROF(iface16), OFFSETOF(iface16), (void*)&interface32_instances[i]->lpVtbl, strstr(result->vtbl16[0].name, "::") - result->vtbl16[0].name, result->vtbl16[0].name);
                return (void*)&interface32_instances[i]->lpVtbl;
            }
        }
    }
    if (!result)
    {
        ERR("unknown interface %s\n", debugstr_guid(riid));
        return 0;
    }
    i32 = (interface_32*)HeapAlloc(GetProcessHeap(), 0, sizeof(interface_32) + IFS_GUARD_SIZE * 2);
    memset(i32, 0xcd, sizeof(interface_32) + IFS_GUARD_SIZE * 2);
    i32 = (interface_32*)((char*)i32 + IFS_GUARD_SIZE);
    if (!result->spVtbl16)
    {
        init_interface_entry(result);
    }
    TRACE("16-bit interface %04x:%04x -> new %p(%.*s)\n", SELECTOROF(iface16), OFFSETOF(iface16), i32, strstr(result->vtbl16[0].name, "::") - result->vtbl16[0].name, result->vtbl16[0].name);
    i32->iface16 = iface16;
    i32->lpVtbl = result->lpVtbl32;
    i32->riid = &result->iid;
    register_instance32(i32);
    return (void*)&i32->lpVtbl;
}
struct hresult_map
{
    HRESULT hresult16;
    HRESULT hresult32;
} hresult_table[] =
{
    { E_UNEXPECTED16, E_UNEXPECTED },
    { E_NOTIMPL16, E_NOTIMPL },
    { E_OUTOFMEMORY16, E_OUTOFMEMORY },
    { E_INVALIDARG16, E_INVALIDARG },
    { E_NOINTERFACE16, E_NOINTERFACE },
    { E_POINTER16, E_POINTER },
    { E_HANDLE16, E_HANDLE },
    { E_ABORT16, E_ABORT },
    { E_FAIL16, E_FAIL },
    { E_ACCESSDENIED16, E_ACCESSDENIED },
};
HRESULT hresult32_16(HRESULT hresult)
{
    int i;
    for (i = 0; i < ARRAYSIZE(hresult_table); i++)
    {
        if (hresult_table[i].hresult32 == hresult)
        {
            TRACE("%08x->%08x\n", hresult, hresult_table[i].hresult16);
            return hresult_table[i].hresult16;
        }
    }
    return hresult;
}

HRESULT hresult16_32(HRESULT hresult)
{
    int i;
    for (i = 0; i < ARRAYSIZE(hresult_table); i++)
    {
        if (hresult_table[i].hresult16 == hresult)
        {
            TRACE("%08x->%08x\n", hresult, hresult_table[i].hresult32);
            return hresult_table[i].hresult32;
        }
    }
    return hresult;
}