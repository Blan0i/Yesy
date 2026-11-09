// Copyright (c) NodeFactory. Commands of the asset window and the Content Browser menu (spec 7.1, 7.2).

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

/**
 * Commands of NodeFactory. The binding context is "NodeFactory" and the style set is FNodeFactoryStyle, so the
 * icons of the commands resolve to the brushes "NodeFactory.<Command>" (NodeFactory.ImportJson, ...).
 *
 * F2 (rename) and Delete of the Pin Manager and the Local Variables panel reuse FGenericCommands; Ctrl+F is
 * FindItem and is only bound inside these panels, the window keeps Ctrl+F of the Blueprint editor (spec 7.1).
 */
class NODEFACTORYEDITOR_API FCNAEditorCommands : public TCommands<FCNAEditorCommands>
{
public:
	FCNAEditorCommands();

	// TCommands
	virtual void RegisterCommands() override;

	/** Import and Overwrite from JSON: summary of the changes, then one Undo transaction (spec 8.2). */
	TSharedPtr<FUICommandInfo> ImportJson;

	/** Export to JSON (spec 8.1). */
	TSharedPtr<FUICommandInfo> ExportJson;

	/** Switches the Live Preview panel on and off (spec 7.1, D10). */
	TSharedPtr<FUICommandInfo> LivePreview;

	/** Full validation of the asset into the Message Log "NodeFactory" (spec 10.2). */
	TSharedPtr<FUICommandInfo> Validate;

	/** Recompile Node: version, compilation and update of all instances (spec 9.1). */
	TSharedPtr<FUICommandInfo> RecompileNode;

	/** Opens the Node Catalog tab (spec 8.3). */
	TSharedPtr<FUICommandInfo> OpenCatalog;

	/** Ctrl+F inside the Pin Manager and the Local Variables panel: focus of the name filter (spec 7.1). */
	TSharedPtr<FUICommandInfo> FindItem;

	/** New input pin on Tunnel Input. */
	TSharedPtr<FUICommandInfo> AddInputPin;

	/** New output pin on Tunnel Output. */
	TSharedPtr<FUICommandInfo> AddOutputPin;

	/** New local variable of the subgraph (spec 4.8). */
	TSharedPtr<FUICommandInfo> AddLocalVariable;

	/** Moves the selected pin one row up or down (keyboard alternative to drag and drop). */
	TSharedPtr<FUICommandInfo> MoveItemUp;
	TSharedPtr<FUICommandInfo> MoveItemDown;
};
