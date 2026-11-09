// Copyright (c) NodeFactory. Commands of the asset window and the Content Browser menu (spec 7.1, 7.2).

#include "CNAEditorCommands.h"
#include "NodeFactoryStyle.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

FCNAEditorCommands::FCNAEditorCommands()
	: TCommands<FCNAEditorCommands>(
		TEXT("NodeFactory"),
		LOCTEXT("Commands.Context", "NodeFactory"),
		NAME_None,
		FNodeFactoryStyle::GetStyleSetName())
{
}

void FCNAEditorCommands::RegisterCommands()
{
	// UI_COMMAND uses LOCTEXT_NAMESPACE as namespace and the command name as key (spec 9.3).
	UI_COMMAND(ImportJson, "Import JSON", "Overwrite this node from a JSON file. A summary of the changes is shown first; the import is one Undo step.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ExportJson, "Export JSON", "Write this node to a readable JSON file (both graphs, settings and metadata).", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(LivePreview, "Live Preview", "Show the node as it appears in a Blueprint graph and update it after every change.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(Validate, "Validate", "Check the asset and write the results into the Message Log NodeFactory.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RecompileNode, "Recompile Node", "Compile the asset, raise its version and offer to update all Blueprints that use the node.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenCatalog, "Node Catalog", "Open the catalog of node presets.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(FindItem, "Find", "Filter the list by name.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::F));
	UI_COMMAND(AddInputPin, "Add Input", "Add an input pin to Tunnel Input of the Execution Graph.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddOutputPin, "Add Output", "Add an output pin to Tunnel Output of the Execution Graph.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddLocalVariable, "Add Variable", "Add a local variable of the subgraph.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(MoveItemUp, "Move Up", "Move the selected pin one row up.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Alt, EKeys::Up));
	UI_COMMAND(MoveItemDown, "Move Down", "Move the selected pin one row down.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Alt, EKeys::Down));
}

#undef LOCTEXT_NAMESPACE
