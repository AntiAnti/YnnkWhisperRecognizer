// (c) Yuri N. K. 2024. All rights reserved.
// ykasczc@gmail.com

#pragma once
 
#include "Modules/ModuleManager.h"

class FYnnkWhisperRecognizerEditor : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

};