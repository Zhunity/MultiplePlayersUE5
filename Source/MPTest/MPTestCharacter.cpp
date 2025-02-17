// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPTestCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// AMPTestCharacter

AMPTestCharacter::AMPTestCharacter() :
	OnCreateSessionCompleteDelegate(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::CreateSessionComplete )),
	OnFindSessionsCompleteDelegate(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::FindGameSessionComplete ))
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)

	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	if(OnlineSubsystem)
	{
		OnlineSessionInterface = OnlineSubsystem->GetSessionInterface();
		UE_LOG(LogTemplateCharacter, Log, TEXT("sssss  ss %s"), *OnlineSubsystem->GetSubsystemName().ToString());
		if(GEngine) // 加上这个启动才不会崩
		{
			GEngine->AddOnScreenDebugMessage(
			-1,
			15.0f,
			FColor::Blue,
			FString::Printf(TEXT("ssss %s"), *OnlineSubsystem->GetSubsystemName().ToString()));
		}
	}
}

void AMPTestCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void AMPTestCharacter::OpenLobby()
{
	if (UWorld* World = GetWorld())
	{
		World->ServerTravel("/Game/ThirdPerson/Maps/Lobby?listen");
	}
}

void AMPTestCharacter::OpenLevel(const FString& Address)
{
	UGameplayStatics::OpenLevel(this, *Address);
}

void AMPTestCharacter::ClientTravel(const FString& Address)
{
	if (APlayerController* PlayerController = GetGameInstance()->GetFirstLocalPlayerController())
	{
		PlayerController->ClientTravel(Address, ETravelType::TRAVEL_Absolute);
	}
}

void AMPTestCharacter::CreateGameSession()
{
	if(! OnlineSessionInterface.IsValid())
	{
		return;
	}
	auto ExistingSession = OnlineSessionInterface->GetNamedSession(NAME_GameSession);
	if(ExistingSession != nullptr)
	{
		OnlineSessionInterface->DestroySession(NAME_GameSession); 
	}
	OnlineSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegate  ); 
	// https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/OnlineSubsystem/FOnlineSessionSettings
	TSharedPtr<FOnlineSessionSettings> SessionSettings = MakeShareable(new FOnlineSessionSettings());
	SessionSettings->bIsLANMatch = false;
	SessionSettings->NumPublicConnections = 4;
	SessionSettings->bAllowJoinInProgress = true;
	SessionSettings->bAllowJoinViaPresence = true;
	SessionSettings->bShouldAdvertise = true;
	SessionSettings->bUsesPresence = true;
	// SessionSettings->bUseLobbiesIfAvailable = true;
	const auto LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	OnlineSessionInterface->CreateSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, *SessionSettings);
}

void AMPTestCharacter::CreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if(bWasSuccessful)
	{
		GEngine->AddOnScreenDebugMessage(-
			1,
			15.0f,
			FColor::Green ,
			FString::Printf(TEXT("CreateSessionComplete success %s "), *SessionName.ToString())
			);
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(-
			1,
			15.0f,
			FColor::Red,
			FString::Printf(TEXT("CreateSessionComplete failed "))
			);
	}
}

void AMPTestCharacter::FindGameSession()
{
	if(!OnlineSessionInterface.IsValid())
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("FindGameSession OnlineSessionInterface IsInvalid"));
		return;
	}
	OnlineSessionInterface->AddOnFindSessionsCompleteDelegate_Handle(OnFindSessionsCompleteDelegate);
	SessionSearchSettings = MakeShareable(new FOnlineSessionSearch()); // 结果存在这里的searchresult里面
	SessionSearchSettings->MaxSearchResults = 10000; // 结果 数量
	SessionSearchSettings->bIsLanQuery = false;
	SessionSearchSettings->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
	
	const auto LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	OnlineSessionInterface->FindSessions(*LocalPlayer->GetPreferredUniqueNetId(), SessionSearchSettings.ToSharedRef()); // 什么意思，搜到的都是在线的？
	UE_LOG(LogTemplateCharacter, Error, TEXT("FindGameSession End"));
} 

void AMPTestCharacter::FindGameSessionComplete(bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		GEngine->AddOnScreenDebugMessage(-
			1,
			15.0f,
			FColor::Red,
			FString::Printf(TEXT("FindGameSessionComplete failed "))
		);
		return;
	}
	UE_LOG(LogTemplateCharacter, Error, TEXT("FindGameSessionComplete %d"), SessionSearchSettings->SearchResults.Num());
	for(auto Result : SessionSearchSettings->SearchResults)
	{
		auto Id = Result.GetSessionIdStr();
		auto User = Result.Session.OwningUserName;
		GEngine->AddOnScreenDebugMessage(-
			1,
			15.0f,
			FColor::Green,
			FString::Printf(TEXT("FindGameSessionComplete id : %s user name : %s"), *Id, *User) 
			);
	}
} 

//////////////////////////////////////////////////////////////////////////
// Input

void AMPTestCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMPTestCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMPTestCharacter::Look);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AMPTestCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	
		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AMPTestCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}