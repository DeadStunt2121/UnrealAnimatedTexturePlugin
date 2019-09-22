/**
 * Animated Texture from GIF file
 *
 * Created by Neil Fang
 * GitHub Repo: https://github.com/neil3d/UnrealAnimatedGIFPlugin
 * GitHub Page: http://neil3d.github.io
 *
*/

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"	// UnrealEd
#include "EditorReimportHandler.h"	// UnrealEd
#include "AnimatedTextureFactory.generated.h"

class UAnimatedTexture2D;

/**
 * Import & Reimport Animated Texture Source, such as .gif file
 */
UCLASS()
class ANIMATEDTEXTUREEDITOR_API UAnimatedTextureFactory : public UFactory, public FReimportHandler
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UFactory Interface
	virtual bool DoesSupportClass(UClass* Class) override;
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual UObject* FactoryCreateBinary(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn) override;
	//~ End UFactory Interface

	//~ Begin FReimportHandler Interface
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	virtual int32 GetPriority() const override;
	//~ End FReimportHandler Interface

private:
	void ImportGIF(UAnimatedTexture2D* TargetTexture, const uint8* Buffer, uint32 BufferSize);
};
