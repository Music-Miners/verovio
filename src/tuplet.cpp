/////////////////////////////////////////////////////////////////////////////
// Name:        tuplet.cpp
// Author:      Rodolfo Zitellini
// Created:     26/06/2012
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "tuplet.h"

//----------------------------------------------------------------------------

#include <assert.h>

//----------------------------------------------------------------------------

#include "beam.h"
#include "btrem.h"
#include "chord.h"
#include "comparison.h"
#include "doc.h"
#include "editorial.h"
#include "elementpart.h"
#include "ftrem.h"
#include "functorparams.h"
#include "note.h"
#include "rest.h"
#include "space.h"
#include "staff.h"
#include "vrv.h"

namespace vrv {

//----------------------------------------------------------------------------
// Tuplet
//----------------------------------------------------------------------------

static const ClassRegistrar<Tuplet> s_factory("tuplet", TUPLET);

Tuplet::Tuplet()
    : LayerElement(TUPLET, "tuplet-")
    , ObjectListInterface()
    , AttColor()
    , AttDurationRatio()
    , AttNumberPlacement()
    , AttTupletVis()
{
    RegisterAttClass(ATT_COLOR);
    RegisterAttClass(ATT_DURATIONRATIO);
    RegisterAttClass(ATT_NUMBERPLACEMENT);
    RegisterAttClass(ATT_TUPLETVIS);

    Reset();
}

Tuplet::~Tuplet() {}

void Tuplet::Reset()
{
    LayerElement::Reset();
    ResetColor();
    ResetDurationRatio();
    ResetNumberPlacement();
    ResetTupletVis();

    m_drawingLeft = NULL;
    m_drawingRight = NULL;
    m_drawingBracketPos = STAFFREL_basic_NONE;
    m_bracketAlignedBeam = NULL;
    m_numAlignedBeam = NULL;
}

bool Tuplet::IsSupportedChild(Object *child)
{
    if (child->Is(BEAM)) {
        assert(dynamic_cast<Beam *>(child));
    }
    else if (child->Is(TUPLET_BRACKET)) {
        assert(dynamic_cast<TupletBracket *>(child));
    }
    else if (child->Is(BTREM)) {
        assert(dynamic_cast<BTrem *>(child));
    }
    else if (child->Is(CHORD)) {
        assert(dynamic_cast<Chord *>(child));
    }
    else if (child->Is(CLEF)) {
        assert(dynamic_cast<Clef *>(child));
    }
    else if (child->Is(FTREM)) {
        assert(dynamic_cast<FTrem *>(child));
    }
    else if (child->Is(NOTE)) {
        assert(dynamic_cast<Note *>(child));
    }
    else if (child->Is(TUPLET_NUM)) {
        assert(dynamic_cast<TupletNum *>(child));
    }
    else if (child->Is(REST)) {
        assert(dynamic_cast<Rest *>(child));
    }
    else if (child->Is(SPACE)) {
        assert(dynamic_cast<Space *>(child));
    }
    else if (child->Is(TUPLET)) {
        assert(dynamic_cast<Tuplet *>(child));
    }
    else if (child->IsEditorialElement()) {
        assert(dynamic_cast<EditorialElement *>(child));
    }
    else {
        return false;
    }
    return true;
}

void Tuplet::AddChild(Object *child)
{
    if (!this->IsSupportedChild(child)) {
        LogError("Adding '%s' to a '%s'", child->GetClassName().c_str(), this->GetClassName().c_str());
        return;
    }

    child->SetParent(this);

    ArrayOfObjects *children = this->GetChildrenForModification();

    // Num and bracket are always added by PrepareLayerElementParts (for now) and we want them to be in the front
    // for the drawing order in the SVG output
    if (child->Is({ TUPLET_BRACKET, TUPLET_NUM })) {
        children->insert(children->begin(), child);
    }
    else {
        children->push_back(child);
    }

    Modify();
}

void Tuplet::AdjustTupletBracketY(Doc *doc, Staff *staff, int staffSize)
{
    TupletBracket *tupletBracket = dynamic_cast<TupletBracket *>(this->FindDescendantByType(TUPLET_BRACKET));
    if (!tupletBracket || (this->GetBracketVisible() == BOOLEAN_false)) return;

    const int yReference = staff->GetDrawingY();
    const int doubleUnit = doc->GetDrawingDoubleUnit(staffSize);
    int bracketVerticalMargin = doubleUnit;
    bracketVerticalMargin *= (m_drawingBracketPos == STAFFREL_basic_above) ? 1 : -1;

    Beam *beam = this->GetBracketAlignedBeam();
    if (beam) {
        // Check for possible articulations
        ListOfObjects artics;
        ClassIdsComparison comparison({ ARTIC });
        this->FindAllDescendantsByComparison(&artics, &comparison);

        int articPadding = 0;
        for (auto &artic : artics) {
            if (!artic->HasSelfBB()) continue;
            if (m_drawingBracketPos == STAFFREL_basic_above) {
                // Left point when slope is going up and right when going down
                int relevantX = (beam->m_beamSegment.m_beamSlope > 0) ? artic->GetSelfLeft() : artic->GetSelfRight();
                int currentYRel = beam->m_beamSegment.m_startingY
                    + beam->m_beamSegment.m_beamSlope * (relevantX - beam->m_beamSegment.m_startingX);
                int articYRel = artic->GetSelfTop();
                articPadding = std::min(currentYRel - articYRel, articPadding);
            }
            else {
                // Right point when slope is going up and left when going down
                int relevantX = (beam->m_beamSegment.m_beamSlope > 0) ? artic->GetSelfRight() : artic->GetSelfLeft();
                int currentYRel = beam->m_beamSegment.m_startingY
                    + beam->m_beamSegment.m_beamSlope * (relevantX - beam->m_beamSegment.m_startingX);
                int articYRel = artic->GetSelfBottom();
                articPadding = std::max(currentYRel - articYRel, articPadding);
            }
        }

        // Check for overlap with rest elements. This might happen when tuplet has rest and beam children that are
        // on the same level in encoding - there might be overlap of bracket with rest in that case
        ListOfObjects descendants;
        ClassIdsComparison rest({ REST });
        this->FindAllDescendantsByComparison(&descendants, &rest);

        int restAdjust = 0;
        const int bracketRel = tupletBracket->GetDrawingYRel() - articPadding + bracketVerticalMargin;
        const int bracketPosition = (tupletBracket->GetSelfTop() + tupletBracket->GetSelfBottom() + bracketRel) / 2;
        for (auto &descendant : descendants) {
            if (descendant->GetFirstAncestor(BEAM) || !descendant->HasSelfBB()) continue;
            if (m_drawingBracketPos == STAFFREL_basic_above) {
                if (bracketPosition < descendant->GetSelfTop()) {
                    const int verticalShift = descendant->GetSelfTop() - bracketPosition;
                    if ((restAdjust == 0) || (restAdjust < verticalShift)) restAdjust = verticalShift;
                }
            }
            else {
                if (bracketPosition > descendant->GetSelfBottom()) {
                    const int verticalShift = descendant->GetSelfBottom() - bracketPosition;
                    if ((restAdjust == 0) || (restAdjust > verticalShift)) restAdjust = verticalShift;
                }
            }
        }
        if (restAdjust) bracketVerticalMargin += restAdjust;

        // Adjust bracket in case beam is horizontal and bracket overlaps with staff line
        if (beam->m_beamSegment.m_beamSlope == 0.0) {
            const int staffHeight = doc->GetDrawingStaffSize(staffSize);
            if ((beam->m_beamSegment.m_startingY < yReference)
                && (beam->m_beamSegment.m_startingY > yReference - staffHeight)
                && !(beam->m_beamSegment.m_startingY % doubleUnit)) {
                bracketVerticalMargin += doubleUnit / 4;
            }
        }

        tupletBracket->SetDrawingYRel(tupletBracket->GetDrawingYRel() - articPadding + bracketVerticalMargin);
    }
    else {
        // Default position is above or below the staff
        int yRel = (m_drawingBracketPos == STAFFREL_basic_above) ? 0 : -doc->GetDrawingStaffSize(staffSize);

        // Check for overlap with content
        // Possible issue with beam above the tuplet - not sure this will be noticeable
        ListOfObjects descendants;
        ClassIdsComparison comparison({ ARTIC, ACCID, BEAM, DOT, FLAG, NOTE, REST, STEM });
        this->FindAllDescendantsByComparison(&descendants, &comparison);

        for (auto &descendant : descendants) {
            if (!descendant->HasSelfBB()) continue;
            if (m_drawingBracketPos == STAFFREL_basic_above) {
                int dist = descendant->GetSelfTop() - yReference;
                if (yRel < dist) yRel = dist;
            }
            else {
                int dist = descendant->GetSelfBottom() - yReference;
                if (yRel > dist) yRel = dist;
            }
        }

        tupletBracket->SetDrawingYRel(tupletBracket->GetDrawingYRel() + yRel + bracketVerticalMargin);
    }
}

void Tuplet::AdjustTupletNumY(Doc *doc, Staff *staff, int staffSize)
{
    TupletNum *tupletNum = dynamic_cast<TupletNum *>(FindDescendantByType(TUPLET_NUM));
    if (!tupletNum || (GetNumVisible() == BOOLEAN_false)) return;

    this->CalculateTupletNumCrossStaff(tupletNum);

    Staff *tupletNumStaff = tupletNum->m_crossStaff ? tupletNum->m_crossStaff : staff;
    const int yReference = tupletNumStaff->GetDrawingY();
    const int doubleUnit = doc->GetDrawingDoubleUnit(staffSize);
    // The num is within a bracket
    if (tupletNum->GetAlignedBracket()) {
        // yRel is not used for drawing but we need to adjust it for the bounding box to follow the changes
        tupletNum->SetDrawingYRel(tupletNum->GetAlignedBracket()->GetDrawingYRel());
        return;
    }

    // The num is on its own
    const int numVerticalMargin = (m_drawingNumPos == STAFFREL_basic_above) ? doubleUnit : -doubleUnit;
    const int staffHeight = doc->GetDrawingStaffSize(staffSize);
    const int adjustedPosition = (m_drawingNumPos == STAFFREL_basic_above) ? 0 : -staffHeight;
    Beam *beam = this->GetNumAlignedBeam();
    if (!beam) {
        tupletNum->SetDrawingYRel(adjustedPosition);
    }

    // Calculate relative Y for the tupletNum
    AdjustTupletNumOverlapParams adjustTupletNumOverlapParams(tupletNum, tupletNumStaff);
    adjustTupletNumOverlapParams.m_horizontalMargin = 2 * doc->GetDrawingUnit(staffSize);
    adjustTupletNumOverlapParams.m_drawingNumPos = m_drawingNumPos;
    adjustTupletNumOverlapParams.m_yRel = tupletNum->GetDrawingY();
    Functor adjustTupletNumOverlap(&Object::AdjustTupletNumOverlap);
    this->Process(&adjustTupletNumOverlap, &adjustTupletNumOverlapParams);
    int yRel = adjustTupletNumOverlapParams.m_yRel - yReference;

    // If we have a beam, see if we can move it to more appropriate position
    if (beam && !m_crossStaff && !FindDescendantByType(ARTIC)) {
        const int xMid = tupletNum->GetDrawingXMid(doc);
        const int yMid = beam->m_beamSegment.m_startingY
            + beam->m_beamSegment.m_beamSlope * (xMid - beam->m_beamSegment.m_startingX);
        const int beamYRel = yMid - yReference + numVerticalMargin;
        if (((m_drawingNumPos == STAFFREL_basic_above) && (beamYRel > 0))
            || ((m_drawingNumPos == STAFFREL_basic_below) && (beamYRel < -staffHeight))) {
            yRel = beamYRel;
        }
    }
    else {
        yRel += numVerticalMargin;
    }

    // If yRel turns out to be too far from the tuplet - try to adjust it accordingly, aligning with the staff
    // top/bottom sides, unless doing so will make tuplet number overlap
    if (((m_drawingNumPos == STAFFREL_basic_below) && (yRel > adjustedPosition))
        || ((m_drawingNumPos == STAFFREL_basic_above) && (yRel < adjustedPosition))) {
        yRel = adjustedPosition;
    }

    tupletNum->SetDrawingYRel(yRel);
}

void Tuplet::FilterList(ArrayOfObjects *childList)
{
    // We want to keep only notes and rests
    // Eventually, we also need to filter out grace notes properly (e.g., with sub-beams)
    ArrayOfObjects::iterator iter = childList->begin();

    while (iter != childList->end()) {
        if (!(*iter)->IsLayerElement() || !(*iter)->HasInterface(INTERFACE_DURATION)) {
            iter = childList->erase(iter);
        }
        else {
            ++iter;
        }
    }
}

void Tuplet::CalculateTupletNumCrossStaff(LayerElement *layerElement)
{
    assert(layerElement);
    // If tuplet is fully cross-staff, just return it - it's enough
    if (m_crossStaff) {
        layerElement->m_crossStaff = m_crossStaff;
        layerElement->m_crossLayer = m_crossLayer;
        return;
    };

    Staff *staff = vrv_cast<Staff *>(this->GetFirstAncestor(STAFF));
    assert(staff);
    // Find if there is a mix of cross-staff and non-cross-staff elements in the tuplet
    ListOfObjects descendants;
    ClassIdsComparison comparison({ CHORD, NOTE, REST });
    this->FindAllDescendantsByComparison(&descendants, &comparison);

    Staff *crossStaff = NULL;
    Layer *crossLayer = NULL;
    int crossStaffCount = 0;
    for (auto object : descendants) {
        LayerElement *durElement = vrv_cast<LayerElement *>(object);
        assert(durElement);
        if (crossStaff && durElement->m_crossStaff && (durElement->m_crossStaff != crossStaff)) {
            crossStaff = NULL;
            // We can stop here
            break;
        }
        else if (durElement->m_crossStaff) {
            ++crossStaffCount;
            crossStaff = durElement->m_crossStaff;
            crossLayer = durElement->m_crossLayer;
        }
    }
    if (!crossStaff) return;

    // In case if most elements of the tuplet are cross-staff we need to make sure there for proper positioning of the
    // tuplet number - otherwise tuplet number can end up with extreme adjustments
    const int descendantCount = static_cast<int>(descendants.size());
    const bool isMostlyCrossStaff = crossStaff && (crossStaffCount > descendantCount / 2);
    if ((isMostlyCrossStaff && this->HasValidTupletNumPosition(crossStaff, staff))
        || (!isMostlyCrossStaff && !this->HasValidTupletNumPosition(staff, crossStaff))) {
        layerElement->m_crossStaff = crossStaff;
        layerElement->m_crossLayer = crossLayer;
    }
}

bool Tuplet::HasValidTupletNumPosition(Staff *preferredStaff, Staff *otherStaff)
{
    Beam *beam = this->GetNumAlignedBeam();
    if (!beam) return true;
    if (beam->m_drawingPlace == BEAMPLACE_mixed) return false;

    if (preferredStaff->GetN() < otherStaff->GetN()) {
        if ((beam->m_drawingPlace == BEAMPLACE_below) && (m_drawingNumPos == STAFFREL_basic_below)) return false;
    }
    else {
        if ((beam->m_drawingPlace == BEAMPLACE_above) && (m_drawingNumPos == STAFFREL_basic_above)) return false;
    }

    return true;
}

void Tuplet::CalcDrawingBracketAndNumPos(bool tupletNumHead)
{
    m_drawingBracketPos = STAFFREL_basic_NONE;

    if (this->HasBracketPlace()) {
        m_drawingBracketPos = this->GetBracketPlace();
    }

    if (this->HasNumPlace()) {
        m_drawingNumPos = this->GetNumPlace();
    }
    else {
        m_drawingNumPos = m_drawingBracketPos;
    }

    // if both are given we are all good (num is set in any case if bracket is)
    if (m_drawingBracketPos != STAFFREL_basic_NONE) {
        return;
    }

    const ArrayOfObjects *tupletChildren = this->GetList(this);

    // There are unbeamed notes of two different beams
    // treat all the notes as unbeamed
    int ups = 0, downs = 0; // quantity of up- and down-stems

    // The first step is to calculate all the stem directions
    // cycle into the elements and count the up and down dirs
    ArrayOfObjects::const_iterator iter = tupletChildren->begin();
    while (iter != tupletChildren->end()) {
        if ((*iter)->Is(CHORD)) {
            Chord *currentChord = vrv_cast<Chord *>(*iter);
            assert(currentChord);
            if (currentChord->GetDrawingStemDir() == STEMDIRECTION_up) {
                ups++;
            }
            else {
                downs++;
            }
        }
        else if ((*iter)->Is(NOTE)) {
            Note *currentNote = vrv_cast<Note *>(*iter);
            assert(currentNote);
            if (!currentNote->IsChordTone() && (currentNote->GetDrawingStemDir() == STEMDIRECTION_up)) {
                ups++;
            }
            if (!currentNote->IsChordTone() && (currentNote->GetDrawingStemDir() == STEMDIRECTION_down)) {
                downs++;
            }
        }
        ++iter;
    }
    // true means up
    m_drawingBracketPos = ups > downs ? STAFFREL_basic_above : STAFFREL_basic_below;

    if (tupletNumHead) {
        m_drawingBracketPos
            = (m_drawingBracketPos == STAFFREL_basic_below) ? STAFFREL_basic_above : STAFFREL_basic_below;
    }

    // also use it for the num unless it is already set
    if (m_drawingNumPos == STAFFREL_basic_NONE) {
        m_drawingNumPos = m_drawingBracketPos;
    }

    return;
}

void Tuplet::GetDrawingLeftRightXRel(int &XRelLeft, int &XRelRight, Doc *doc) const
{
    assert(m_drawingLeft);
    assert(m_drawingRight);

    XRelLeft = 0;

    if (m_drawingLeft->Is(NOTE)) {
        //
    }
    else if (m_drawingLeft->Is(REST)) {
        //
    }
    else if (m_drawingLeft->Is(CHORD)) {
        Chord *chord = vrv_cast<Chord *>(m_drawingLeft);
        assert(chord);
        XRelLeft = chord->GetXMin() - m_drawingLeft->GetDrawingX();
    }

    XRelRight = 0;

    if (m_drawingRight->Is(NOTE)) {
        XRelRight += (2 * m_drawingRight->GetDrawingRadius(doc));
    }
    else if (m_drawingRight->Is(REST)) {
        XRelRight += m_drawingRight->GetSelfX2();
    }
    else if (m_drawingRight->Is(CHORD)) {
        Chord *chord = vrv_cast<Chord *>(m_drawingRight);
        assert(chord);
        XRelRight = chord->GetXMax() - chord->GetDrawingX() + (2 * chord->GetDrawingRadius(doc));
    }
}

//----------------------------------------------------------------------------
// Functors methods
//----------------------------------------------------------------------------

int Tuplet::PrepareLayerElementParts(FunctorParams *functorParams)
{
    TupletBracket *currentBracket = dynamic_cast<TupletBracket *>(this->FindDescendantByType(TUPLET_BRACKET, 1));
    TupletNum *currentNum = dynamic_cast<TupletNum *>(this->FindDescendantByType(TUPLET_NUM, 1));

    bool beamed = false;
    // Are we contained in a beam?
    if (this->GetFirstAncestor(BEAM, MAX_BEAM_DEPTH)) {
        // is only the tuplet beamed? (will not work with nested tuplets)
        Beam *currentBeam = dynamic_cast<Beam *>(this->GetFirstAncestor(BEAM, MAX_BEAM_DEPTH));
        if (currentBeam->GetChildCount() == 1) {
            beamed = true;
        }
    }
    // Is a beam or bTrem the only child? (will not work with editorial elements)
    if (this->GetChildCount() == 1) {
        if ((this->GetChildCount(BEAM) == 1) || (this->GetChildCount(BTREM) == 1)) beamed = true;
    }

    if ((!this->HasBracketVisible() && !beamed) || (this->GetBracketVisible() == BOOLEAN_true)) {
        if (!currentBracket) {
            currentBracket = new TupletBracket();
            this->AddChild(currentBracket);
        }
        currentBracket->AttTupletVis::operator=(*this);
    }
    // This will happen only if the @bracket.visible value has changed
    else if (currentBracket) {
        if (this->DeleteChild(currentBracket)) {
            currentBracket = NULL;
        }
    }

    if (this->HasNum() && (!this->HasNumVisible() || (this->GetNumVisible() == BOOLEAN_true))) {
        if (!currentNum) {
            currentNum = new TupletNum();
            this->AddChild(currentNum);
        }
        currentNum->AttNumberPlacement::operator=(*this);
        currentNum->AttTupletVis::operator=(*this);
    }
    // This will happen only if the @num.visible value has changed
    else if (currentNum) {
        if (this->DeleteChild(currentNum)) {
            currentNum = NULL;
        }
    }

    /************ Prepare the drawing cue size ************/

    Functor prepareDrawingCueSize(&Object::PrepareDrawingCueSize);
    this->Process(&prepareDrawingCueSize, NULL);

    /*********** Get the left and right element ***********/

    ClassIdsComparison comparison({ CHORD, NOTE, REST });
    m_drawingLeft = dynamic_cast<LayerElement *>(this->FindDescendantByComparison(&comparison));
    m_drawingRight
        = dynamic_cast<LayerElement *>(this->FindDescendantByComparison(&comparison, UNLIMITED_DEPTH, BACKWARD));

    return FUNCTOR_CONTINUE;
}

int Tuplet::AdjustTupletsX(FunctorParams *functorParams)
{
    FunctorDocParams *params = vrv_params_cast<FunctorDocParams *>(functorParams);
    assert(params);

    // Nothing to do if there is no number
    if (!this->HasNum()) {
        return FUNCTOR_SIBLINGS;
    }

    // Nothing to do if the bracket and the num are not visible
    if ((this->GetBracketVisible() == BOOLEAN_false) && (this->GetNumVisible() == BOOLEAN_false)) {
        return FUNCTOR_SIBLINGS;
    }

    // Nothing we can to the pointers to the left and right are not set
    if (!this->GetDrawingLeft() || !this->GetDrawingRight()) {
        return FUNCTOR_SIBLINGS;
    }

    assert(m_drawingBracketPos != STAFFREL_basic_NONE);

    // Carefull: this will not work if the tuplet has editorial markup (one child) and then notes + one beam
    Beam *beamParent = dynamic_cast<Beam *>(this->GetFirstAncestor(BEAM, MAX_BEAM_DEPTH));
    // Are we contained in a beam?
    if (beamParent) {
        m_bracketAlignedBeam = beamParent;
    }
    Beam *beamChild = dynamic_cast<Beam *>(this->FindDescendantByType(BEAM));
    // Do we contain a beam?
    if (beamChild) {
        if ((this->GetChildCount(NOTE) == 0) && (this->GetChildCount(CHORD) == 0) && (this->GetChildCount(BEAM) == 1)) {
            m_bracketAlignedBeam = beamChild;
        }
    }

    m_numAlignedBeam = m_bracketAlignedBeam;

    // Cancel alignment of the bracket with the beam if position and stemdirection are not concordant
    if (m_bracketAlignedBeam && (m_bracketAlignedBeam->m_drawingPlace == BEAMPLACE_above)
        && (m_drawingBracketPos == STAFFREL_basic_below)) {
        m_bracketAlignedBeam = NULL;
    }
    else if (m_bracketAlignedBeam
        && ((m_bracketAlignedBeam->m_drawingPlace == BEAMPLACE_below)
            && (m_drawingBracketPos == STAFFREL_basic_above))) {
        m_bracketAlignedBeam = NULL;
    }

    // Cancel alignment of the num with the beam if position and stemdirection are not concordant
    if (m_numAlignedBeam && (m_numAlignedBeam->m_drawingPlace == BEAMPLACE_above)
        && (m_drawingNumPos == STAFFREL_basic_below)) {
        m_numAlignedBeam = NULL;
    }
    else if (m_numAlignedBeam
        && ((m_numAlignedBeam->m_drawingPlace == BEAMPLACE_below) && (m_drawingNumPos == STAFFREL_basic_above))) {
        m_numAlignedBeam = NULL;
    }

    int XRelLeft;
    int XRelRight;
    this->GetDrawingLeftRightXRel(XRelLeft, XRelRight, params->m_doc);

    TupletBracket *tupletBracket = dynamic_cast<TupletBracket *>(this->FindDescendantByType(TUPLET_BRACKET));
    if (tupletBracket && (this->GetBracketVisible() != BOOLEAN_false)) {
        tupletBracket->SetDrawingXRelLeft(XRelLeft);
        tupletBracket->SetDrawingXRelRight(XRelRight);
    }

    TupletNum *tupletNum = dynamic_cast<TupletNum *>(this->FindDescendantByType(TUPLET_NUM));
    if (tupletNum && (this->GetNumVisible() != BOOLEAN_false)) {
        // We have a bracket and the num is not on its opposite side
        if (tupletBracket && (m_drawingNumPos == m_drawingBracketPos)) {
            tupletNum->SetAlignedBracket(tupletBracket);
        }
        else {
            tupletNum->SetAlignedBracket(NULL);
        }
    }

    return FUNCTOR_SIBLINGS;
}

int Tuplet::AdjustTupletsY(FunctorParams *functorParams)
{
    FunctorDocParams *params = vrv_params_cast<FunctorDocParams *>(functorParams);
    assert(params);

    // Nothing to do if there is no number
    if (!this->HasNum()) {
        return FUNCTOR_SIBLINGS;
    }

    // Nothing to do if the bracket and the num are not visible
    if ((this->GetBracketVisible() == BOOLEAN_false) && (this->GetNumVisible() == BOOLEAN_false)) {
        return FUNCTOR_SIBLINGS;
    }

    if (!this->GetDrawingLeft() || !this->GetDrawingRight()) {
        return FUNCTOR_SIBLINGS;
    }

    Staff *staff = vrv_cast<Staff *>(this->GetFirstAncestor(STAFF));
    assert(staff);
    int staffSize = staff->m_drawingStaffSize;

    assert(m_drawingBracketPos != STAFFREL_basic_NONE);

    Staff *relevantStaff = m_crossStaff ? m_crossStaff : staff;

    this->AdjustTupletBracketY(params->m_doc, relevantStaff, staffSize);

    this->AdjustTupletNumY(params->m_doc, relevantStaff, staffSize);

    return FUNCTOR_SIBLINGS;
}

int Tuplet::ResetDrawing(FunctorParams *functorParams)
{
    // Call parent one too
    LayerElement::ResetDrawing(functorParams);

    // We want the list of the ObjectListInterface to be re-generated
    this->Modify();

    m_drawingLeft = NULL;
    m_drawingRight = NULL;

    return FUNCTOR_CONTINUE;
}

int Tuplet::ResetHorizontalAlignment(FunctorParams *functorParams)
{
    // Call parent one too
    LayerElement::ResetHorizontalAlignment(functorParams);

    m_drawingBracketPos = STAFFREL_basic_NONE;
    m_bracketAlignedBeam = NULL;
    m_numAlignedBeam = NULL;

    return FUNCTOR_CONTINUE;
}

} // namespace vrv
