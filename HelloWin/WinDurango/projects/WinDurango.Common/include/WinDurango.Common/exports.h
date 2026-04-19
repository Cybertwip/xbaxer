#pragma once

#ifdef WD_API_EXPORTS
#define WD_API __declspec(dllexport)
#else
#define WD_API __declspec(dllimport)
#endif