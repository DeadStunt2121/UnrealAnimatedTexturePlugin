
#include "AnimatedGIFDecoder.h"
#include "AnimatedTextureModule.h"
#include "RenderingThread.h"	// RenderCore

enum EGIF_Mode
{
	GIF_NONE = 0,
	GIF_CURR = 1,
	GIF_BKGD = 2,
	GIF_PREV = 3
};

void UAnimatedGIFDecoder::Import_Init(uint32 InGlobalWidth, uint32 InGlobalHeight, uint8 InBackground, uint32 InFrameCount)
{
	GlobalWidth = InGlobalWidth;
	GlobalHeight = InGlobalHeight;
	Background = InBackground;

	Frames.Init(FGIFFrame(), InFrameCount);
}

void UAnimatedGIFDecoder::Import_Finished()
{
	Duration = 0.0f;
	for (const auto& Frm : Frames)
		Duration += Frm.Time;
}

float UAnimatedGIFDecoder::GetFrameDelay(int FrameIndex) const
{
	const FGIFFrame& Frame = Frames[FrameIndex];
	return Frame.Time;
}

void UAnimatedGIFDecoder::DecodeFrameToRHI(FTextureResource * RHIResource, FAnmatedTextureState& AnimState, bool SupportsTransparency)
{
	if (FrameBuffer[0].Num() != GlobalHeight * GlobalWidth) {
		LastFrame = 0;

		FColor BGColor(0L);
		const FGIFFrame& GIFFrame = Frames[0];
		if (!SupportsTransparency || GIFFrame.TransparentIndex == -1)
			BGColor = GIFFrame.Palette[Background];

		for (int i = 0; i < 2; i++)
			FrameBuffer[i].Init(BGColor, GlobalHeight*GlobalWidth);
	}

	//-- Render Command Data
	struct FRenderCommandData
	{
		FTextureResource * RHIResource;
		FGIFFrame* GIFFrame;
		UAnimatedGIFDecoder* Decoder;
		bool FirstFrame;
		bool SupportsTransparency;
	};

	typedef TSharedPtr<FRenderCommandData, ESPMode::ThreadSafe> FCommandDataPtr;
	FCommandDataPtr CommandData = MakeShared<FRenderCommandData, ESPMode::ThreadSafe>();

	CommandData->GIFFrame = &Frames[AnimState.CurrentFrame];
	CommandData->RHIResource = RHIResource;
	CommandData->Decoder = this;
	CommandData->FirstFrame = AnimState.CurrentFrame == 0;
	CommandData->SupportsTransparency = SupportsTransparency;

	//-- Equeue command
	ENQUEUE_RENDER_COMMAND(DecodeGIFFrameToTexture)(
		[CommandData](FRHICommandListImmediate& RHICmdList)
	{
		if (!CommandData->RHIResource || !CommandData->RHIResource->TextureRHI)
			return;

		FTexture2DRHIRef Texture2DRHI = CommandData->RHIResource->TextureRHI->GetTexture2D();
		if (!Texture2DRHI)
			return;

		FGIFFrame& GIFFrame = *(CommandData->GIFFrame);
		uint32& InLastFrame = CommandData->Decoder->LastFrame;
		bool SupportsTransparency = CommandData->SupportsTransparency;

		FColor* PICT = CommandData->Decoder->FrameBuffer[InLastFrame].GetData();
		uint32 InBackground = CommandData->Decoder->Background;

		TArray<FColor>& Pal = GIFFrame.Palette;

		uint32 TexWidth = Texture2DRHI->GetSizeX();
		uint32 TexHeight = Texture2DRHI->GetSizeY();

		if (SupportsTransparency && GIFFrame.TransparentIndex != -1)
			Pal[GIFFrame.TransparentIndex].A = 0;

		//-- decode to frame buffer
		uint32 DDest = TexWidth * GIFFrame.OffsetY + GIFFrame.OffsetX;
		uint32 Src = 0;
		uint32 Iter = GIFFrame.Interlacing ? 0 : 4;
		uint32 Fin = !Iter ? 4 : 5;

		for (; Iter < Fin; Iter++) // interlacing support
		{
			uint32 YOffset = 16U >> ((Iter > 1) ? Iter : 1);

			for (uint32 Y = (8 >> Iter) & 7; Y < GIFFrame.Height; Y += YOffset)
			{
				for (uint32 X = 0; X < GIFFrame.Width; X++)
				{
					uint32 TexIndex = TexWidth * Y + X + DDest;
					uint8 ColorIndex = GIFFrame.PixelIndices[Src];

					PICT[TexIndex] = Pal[ColorIndex];

					Src++;
				}// end of for(x)
			}// end of for(y)
		}// end of for(iter)

		//-- write texture
		uint32 DestPitch = 0;
		FColor* SrcBuffer = PICT;
		FColor* DestBuffer = (FColor*)RHILockTexture2D(Texture2DRHI, 0, RLM_WriteOnly, DestPitch, false);
		if (DestBuffer)
		{
			if (DestPitch == TexWidth * sizeof(FColor))
			{
				FMemory::Memcpy(DestBuffer, SrcBuffer, TexWidth*TexHeight * sizeof(FColor));
			}
			else
			{
				// copy row by row
				uint32 SrcPitch = TexWidth * sizeof(FColor);
				for (uint32 y = 0; y < TexHeight; y++)
				{
					FMemory::Memcpy(DestBuffer, SrcBuffer, TexWidth * sizeof(FColor));
					DestBuffer += DestPitch;
					SrcBuffer += SrcPitch;
				}// end of for
			}// end of else

			RHIUnlockTexture2D(Texture2DRHI, 0, false);
		}// end of if
		else
		{
			UE_LOG(LogAnimTexture, Warning, TEXT("Unable to lock texture for write"));
		}// end of else

		//-- frame blending
		EGIF_Mode Mode = (EGIF_Mode)GIFFrame.Mode;

		if (CommandData->FirstFrame)	// loop restart
			Mode = GIF_BKGD;

		switch (Mode)
		{
		case GIF_NONE:
		case GIF_CURR:
			break;
		case GIF_BKGD:	// restore background
		{
			FColor BGColor(0L);

			if (SupportsTransparency)
			{
				if(GIFFrame.TransparentIndex == -1)
					BGColor = GIFFrame.Palette[InBackground];
				else
					BGColor = GIFFrame.Palette[GIFFrame.TransparentIndex];
			}
			else
			{
				BGColor = GIFFrame.Palette[InBackground];
			}

			uint32 BGWidth = GIFFrame.Width;
			uint32 BGHeight = GIFFrame.Height;
			uint32 XDest = DDest;

			if (CommandData->FirstFrame)
			{
				BGWidth = TexWidth;
				BGHeight = TexHeight;
				XDest = 0;
			}

			for (uint32 Y = 0; Y < BGHeight; Y++)
			{
				for (uint32 X = 0; X < BGWidth; X++)
				{
					PICT[TexWidth * Y + X + XDest] = BGColor;
				}// end of for(x)
			}// end of for(y)
		}
		break;
		case GIF_PREV:	// restore prevous frame
			InLastFrame = (InLastFrame + 1) % 2;
			break;
		default:
			UE_LOG(LogAnimTexture, Warning, TEXT("Unknown GIF Mode"));
			break;
		}//end of switch

		if (SupportsTransparency && GIFFrame.TransparentIndex != -1)
			Pal[GIFFrame.TransparentIndex].A = 255;
	}
	);
}

void UAnimatedGIFDecoder::Serialize(FArchive & Ar)
{
	Super::Serialize(Ar);

	Ar << Frames;
}
