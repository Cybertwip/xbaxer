#pragma once

#ifdef WD_API_EXPORTS
#define WD_API __declspec(dllexport)
#else
#define WD_API __declspec(dllimport)
#endif

#ifdef WDIMPL_API_EXPORTS
#define WDIMPL_API __declspec(dllexport)
#else
#define WDIMPL_API __declspec(dllimport)
#endif