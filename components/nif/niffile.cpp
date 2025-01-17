#include "niffile.hpp"

#include <components/debug/debuglog.hpp>
#include <components/files/hash.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

#include "controller.hpp"
#include "data.hpp"
#include "effect.hpp"
#include "exception.hpp"
#include "extra.hpp"
#include "node.hpp"
#include "particle.hpp"
#include "physics.hpp"
#include "property.hpp"
#include "texture.hpp"

namespace Nif
{

    Reader::Reader(NIFFile& file)
        : mVersion(file.mVersion)
        , mUserVersion(file.mUserVersion)
        , mBethVersion(file.mBethVersion)
        , mFilename(file.mPath)
        , mHash(file.mHash)
        , mRecords(file.mRecords)
        , mRoots(file.mRoots)
        , mUseSkinning(file.mUseSkinning)
    {
    }

    template <typename NodeType, RecordType recordType>
    static std::unique_ptr<Record> construct()
    {
        auto result = std::make_unique<NodeType>();
        result->recType = recordType;
        return result;
    }

    using CreateRecord = std::unique_ptr<Record> (*)();

    /// These are all the record types we know how to read.
    static std::map<std::string, CreateRecord> makeFactory()
    {
        return {
            // 4.0.0.2 refers to Bethesda variant of NetImmerse 4.0.0.2 file format
            // Gamebryo refers to files newer than 4.0.0.2
            // Bethesda refers to custom records Bethesda introduced post-4.0.0.2

            // NODES

            // NiNode-like nodes, 4.0.0.2
            { "NiNode", &construct<NiNode, RC_NiNode> },
            { "AvoidNode", &construct<NiNode, RC_AvoidNode> },
            { "NiBillboardNode", &construct<NiBillboardNode, RC_NiBillboardNode> },
            { "NiBSAnimationNode", &construct<NiNode, RC_NiBSAnimationNode> },
            { "NiBSParticleNode", &construct<NiNode, RC_NiBSParticleNode> },
            { "NiCollisionSwitch", &construct<NiNode, RC_NiCollisionSwitch> },
            { "NiSortAdjustNode", &construct<NiSortAdjustNode, RC_NiSortAdjustNode> },
            { "RootCollisionNode", &construct<NiNode, RC_RootCollisionNode> },

            // NiNode-like nodes, Bethesda
            { "BSBlastNode", &construct<BSRangeNode, RC_NiNode> },
            { "BSDamageStage", &construct<BSRangeNode, RC_NiNode> },
            { "BSFadeNode", &construct<NiNode, RC_NiNode> },
            { "BSLeafAnimNode", &construct<NiNode, RC_NiNode> },
            { "BSMultiBoundNode", &construct<BSMultiBoundNode, RC_NiNode> },
            { "BSOrderedNode", &construct<BSOrderedNode, RC_NiNode> },
            { "BSRangeNode", &construct<BSRangeNode, RC_NiNode> },
            { "BSTreeNode", &construct<BSTreeNode, RC_NiNode> },
            { "BSValueNode", &construct<BSValueNode, RC_NiNode> },

            // Switch nodes, 4.0.0.2
            { "NiSwitchNode", &construct<NiSwitchNode, RC_NiSwitchNode> },
            { "NiFltAnimationNode", &construct<NiFltAnimationNode, RC_NiFltAnimationNode> },
            { "NiLODNode", &construct<NiLODNode, RC_NiLODNode> },

            // NiSequence nodes, 4.0.0.2
            { "NiSequenceStreamHelper", &construct<NiSequenceStreamHelper, RC_NiSequenceStreamHelper> },

            // NiSequence nodes, Gamebryo
            { "NiSequence", &construct<NiSequence, RC_NiSequence> },
            { "NiControllerSequence", &construct<NiControllerSequence, RC_NiControllerSequence> },

            // Other nodes, 4.0.0.2
            { "NiCamera", &construct<NiCamera, RC_NiCamera> },

            // ACCUMULATORS

            // 4.0.0.2
            { "NiAlphaAccumulator", &construct<NiAlphaAccumulator, RC_NiAlphaAccumulator> },
            { "NiClusterAccumulator", &construct<NiClusterAccumulator, RC_NiClusterAccumulator> },

            // CONTROLLERS

            // 4.0.0.2
            { "NiAlphaController", &construct<NiAlphaController, RC_NiAlphaController> },
            { "NiBSPArrayController", &construct<NiBSPArrayController, RC_NiBSPArrayController> },
            { "NiFlipController", &construct<NiFlipController, RC_NiFlipController> },
            { "NiGeomMorpherController", &construct<NiGeomMorpherController, RC_NiGeomMorpherController> },
            { "NiKeyframeController", &construct<NiKeyframeController, RC_NiKeyframeController> },
            { "NiLookAtController", &construct<NiLookAtController, RC_NiLookAtController> },
            { "NiMaterialColorController", &construct<NiMaterialColorController, RC_NiMaterialColorController> },
            { "NiParticleSystemController", &construct<NiParticleSystemController, RC_NiParticleSystemController> },
            { "NiPathController", &construct<NiPathController, RC_NiPathController> },
            { "NiRollController", &construct<NiRollController, RC_NiRollController> },
            { "NiUVController", &construct<NiUVController, RC_NiUVController> },
            { "NiVisController", &construct<NiVisController, RC_NiVisController> },

            // Gamebryo
            { "NiControllerManager", &construct<NiControllerManager, RC_NiControllerManager> },
            { "NiTransformController", &construct<NiKeyframeController, RC_NiKeyframeController> },
            { "NiTextureTransformController",
                &construct<NiTextureTransformController, RC_NiTextureTransformController> },
            { "NiMultiTargetTransformController",
                &construct<NiMultiTargetTransformController, RC_NiMultiTargetTransformController> },

            // Bethesda
            { "BSMaterialEmittanceMultController",
                &construct<NiFloatInterpController, RC_BSMaterialEmittanceMultController> },
            { "BSRefractionFirePeriodController",
                &construct<NiSingleInterpController, RC_BSRefractionFirePeriodController> },
            { "BSRefractionStrengthController",
                &construct<NiFloatInterpController, RC_BSRefractionStrengthController> },
            { "BSEffectShaderPropertyColorController",
                &construct<BSEffectShaderPropertyColorController, RC_BSEffectShaderPropertyColorController> },
            { "BSEffectShaderPropertyFloatController",
                &construct<BSEffectShaderPropertyFloatController, RC_BSEffectShaderPropertyFloatController> },
            { "BSLightingShaderPropertyColorController",
                &construct<BSEffectShaderPropertyColorController, RC_BSLightingShaderPropertyColorController> },
            { "BSLightingShaderPropertyFloatController",
                &construct<BSEffectShaderPropertyFloatController, RC_BSLightingShaderPropertyFloatController> },
            { "bhkBlendController", &construct<bhkBlendController, RC_bhkBlendController> },

            // Interpolators, Gamebryo
            { "NiBlendBoolInterpolator", &construct<NiBlendBoolInterpolator, RC_NiBlendBoolInterpolator> },
            { "NiBlendFloatInterpolator", &construct<NiBlendFloatInterpolator, RC_NiBlendFloatInterpolator> },
            { "NiBlendPoint3Interpolator", &construct<NiBlendPoint3Interpolator, RC_NiBlendPoint3Interpolator> },
            { "NiBlendTransformInterpolator",
                &construct<NiBlendTransformInterpolator, RC_NiBlendTransformInterpolator> },
            { "NiBoolInterpolator", &construct<NiBoolInterpolator, RC_NiBoolInterpolator> },
            { "NiBoolTimelineInterpolator", &construct<NiBoolInterpolator, RC_NiBoolTimelineInterpolator> },
            { "NiColorInterpolator", &construct<NiColorInterpolator, RC_NiColorInterpolator> },
            { "NiFloatInterpolator", &construct<NiFloatInterpolator, RC_NiFloatInterpolator> },
            { "NiPoint3Interpolator", &construct<NiPoint3Interpolator, RC_NiPoint3Interpolator> },
            { "NiTransformInterpolator", &construct<NiTransformInterpolator, RC_NiTransformInterpolator> },

            // DATA

            // 4.0.0.2
            { "NiColorData", &construct<NiColorData, RC_NiColorData> },
            { "NiFloatData", &construct<NiFloatData, RC_NiFloatData> },
            { "NiKeyframeData", &construct<NiKeyframeData, RC_NiKeyframeData> },
            { "NiMorphData", &construct<NiMorphData, RC_NiMorphData> },
            { "NiPalette", &construct<NiPalette, RC_NiPalette> },
            { "NiPixelData", &construct<NiPixelData, RC_NiPixelData> },
            { "NiPosData", &construct<NiPosData, RC_NiPosData> },
            { "NiSourceTexture", &construct<NiSourceTexture, RC_NiSourceTexture> },
            { "NiUVData", &construct<NiUVData, RC_NiUVData> },
            { "NiVisData", &construct<NiVisData, RC_NiVisData> },

            // Gamebryo
            { "NiBoolData", &construct<NiBoolData, RC_NiBoolData> },
            { "NiDefaultAVObjectPalette", &construct<NiDefaultAVObjectPalette, RC_NiDefaultAVObjectPalette> },
            { "NiTransformData", &construct<NiKeyframeData, RC_NiKeyframeData> },

            // Bethesda
            { "BSShaderTextureSet", &construct<BSShaderTextureSet, RC_BSShaderTextureSet> },

            // DYNAMIC EFFECTS

            // 4.0.0.2
            { "NiAmbientLight", &construct<NiLight, RC_NiLight> },
            { "NiDirectionalLight", &construct<NiLight, RC_NiLight> },
            { "NiPointLight", &construct<NiPointLight, RC_NiLight> },
            { "NiSpotLight", &construct<NiSpotLight, RC_NiLight> },
            { "NiTextureEffect", &construct<NiTextureEffect, RC_NiTextureEffect> },

            // EXTRA DATA

            // 4.0.0.2
            { "NiExtraData", &construct<NiExtraData, RC_NiExtraData> },
            { "NiStringExtraData", &construct<NiStringExtraData, RC_NiStringExtraData> },
            { "NiTextKeyExtraData", &construct<NiTextKeyExtraData, RC_NiTextKeyExtraData> },
            { "NiVertWeightsExtraData", &construct<NiVertWeightsExtraData, RC_NiVertWeightsExtraData> },

            // Gamebryo
            { "NiBinaryExtraData", &construct<NiBinaryExtraData, RC_NiBinaryExtraData> },
            { "NiBooleanExtraData", &construct<NiBooleanExtraData, RC_NiBooleanExtraData> },
            { "NiColorExtraData", &construct<NiVectorExtraData, RC_NiColorExtraData> },
            { "NiFloatExtraData", &construct<NiFloatExtraData, RC_NiFloatExtraData> },
            { "NiFloatsExtraData", &construct<NiFloatsExtraData, RC_NiFloatsExtraData> },
            { "NiIntegerExtraData", &construct<NiIntegerExtraData, RC_NiIntegerExtraData> },
            { "NiIntegersExtraData", &construct<NiIntegersExtraData, RC_NiIntegersExtraData> },
            { "NiVectorExtraData", &construct<NiVectorExtraData, RC_NiVectorExtraData> },
            { "NiStringPalette", &construct<NiStringPalette, RC_NiStringPalette> },

            // Bethesda bounds
            { "BSBound", &construct<BSBound, RC_BSBound> },
            { "BSMultiBound", &construct<BSMultiBound, RC_BSMultiBound> },
            { "BSMultiBoundOBB", &construct<BSMultiBoundOBB, RC_BSMultiBoundOBB> },
            { "BSMultiBoundSphere", &construct<BSMultiBoundSphere, RC_BSMultiBoundSphere> },

            // Bethesda markers
            { "BSFurnitureMarker", &construct<BSFurnitureMarker, RC_BSFurnitureMarker> },
            { "BSFurnitureMarkerNode", &construct<BSFurnitureMarker, RC_BSFurnitureMarker> },
            { "BSInvMarker", &construct<BSInvMarker, RC_BSInvMarker> },

            // Other Bethesda records
            { "BSBehaviorGraphExtraData", &construct<BSBehaviorGraphExtraData, RC_BSBehaviorGraphExtraData> },
            { "BSXFlags", &construct<NiIntegerExtraData, RC_BSXFlags> },

            // GEOMETRY

            // 4.0.0.2
            { "NiAutoNormalParticles", &construct<NiParticles, RC_NiParticles> },
            { "NiAutoNormalParticlesData", &construct<NiParticlesData, RC_NiParticlesData> },
            { "NiLines", &construct<NiLines, RC_NiLines> },
            { "NiLinesData", &construct<NiLinesData, RC_NiLinesData> },
            { "NiParticles", &construct<NiParticles, RC_NiParticles> },
            { "NiParticlesData", &construct<NiParticlesData, RC_NiParticlesData> },
            { "NiRotatingParticles", &construct<NiParticles, RC_NiParticles> },
            { "NiRotatingParticlesData", &construct<NiRotatingParticlesData, RC_NiParticlesData> },
            { "NiSkinData", &construct<NiSkinData, RC_NiSkinData> },
            { "NiSkinInstance", &construct<NiSkinInstance, RC_NiSkinInstance> },
            { "NiSkinPartition", &construct<NiSkinPartition, RC_NiSkinPartition> },
            { "NiTriShape", &construct<NiTriShape, RC_NiTriShape> },
            { "NiTriShapeData", &construct<NiTriShapeData, RC_NiTriShapeData> },
            { "NiTriStrips", &construct<NiTriStrips, RC_NiTriStrips> },
            { "NiTriStripsData", &construct<NiTriStripsData, RC_NiTriStripsData> },

            // Bethesda
            { "BSDismemberSkinInstance", &construct<BSDismemberSkinInstance, RC_BSDismemberSkinInstance> },
            { "BSTriShape", &construct<BSTriShape, RC_BSTriShape> },
            { "BSLODTriShape", &construct<BSLODTriShape, RC_BSLODTriShape> },

            // PARTICLES

            // Modifiers, 4.0.0.2
            { "NiGravity", &construct<NiGravity, RC_NiGravity> },
            { "NiParticleColorModifier", &construct<NiParticleColorModifier, RC_NiParticleColorModifier> },
            { "NiParticleGrowFade", &construct<NiParticleGrowFade, RC_NiParticleGrowFade> },
            { "NiParticleRotation", &construct<NiParticleRotation, RC_NiParticleRotation> },

            // Colliders, 4.0.0.2
            { "NiPlanarCollider", &construct<NiPlanarCollider, RC_NiPlanarCollider> },
            { "NiSphericalCollider", &construct<NiSphericalCollider, RC_NiSphericalCollider> },

            // PHYSICS

            // Collision objects, Gamebryo
            { "NiCollisionObject", &construct<NiCollisionObject, RC_NiCollisionObject> },

            // Collision objects, Bethesda
            { "bhkCollisionObject", &construct<bhkCollisionObject, RC_bhkCollisionObject> },
            { "bhkPCollisionObject", &construct<bhkCollisionObject, RC_bhkCollisionObject> },
            { "bhkSPCollisionObject", &construct<bhkCollisionObject, RC_bhkCollisionObject> },

            // Constraint records, Bethesda
            { "bhkHingeConstraint", &construct<bhkHingeConstraint, RC_bhkHingeConstraint> },
            { "bhkLimitedHingeConstraint", &construct<bhkLimitedHingeConstraint, RC_bhkLimitedHingeConstraint> },
            { "bhkRagdollConstraint", &construct<bhkRagdollConstraint, RC_bhkRagdollConstraint> },

            // Physics body records, Bethesda
            { "bhkRigidBody", &construct<bhkRigidBody, RC_bhkRigidBody> },
            { "bhkRigidBodyT", &construct<bhkRigidBody, RC_bhkRigidBodyT> },

            // Physics geometry records, Bethesda
            { "bhkBoxShape", &construct<bhkBoxShape, RC_bhkBoxShape> },
            { "bhkCapsuleShape", &construct<bhkCapsuleShape, RC_bhkCapsuleShape> },
            { "bhkCompressedMeshShape", &construct<bhkCompressedMeshShape, RC_bhkCompressedMeshShape> },
            { "bhkCompressedMeshShapeData", &construct<bhkCompressedMeshShapeData, RC_bhkCompressedMeshShapeData> },
            { "bhkConvexTransformShape", &construct<bhkConvexTransformShape, RC_bhkConvexTransformShape> },
            { "bhkConvexVerticesShape", &construct<bhkConvexVerticesShape, RC_bhkConvexVerticesShape> },
            { "bhkListShape", &construct<bhkListShape, RC_bhkListShape> },
            { "bhkMoppBvTreeShape", &construct<bhkMoppBvTreeShape, RC_bhkMoppBvTreeShape> },
            { "bhkNiTriStripsShape", &construct<bhkNiTriStripsShape, RC_bhkNiTriStripsShape> },
            { "bhkPackedNiTriStripsShape", &construct<bhkPackedNiTriStripsShape, RC_bhkPackedNiTriStripsShape> },
            { "hkPackedNiTriStripsData", &construct<hkPackedNiTriStripsData, RC_hkPackedNiTriStripsData> },
            { "bhkSimpleShapePhantom", &construct<bhkSimpleShapePhantom, RC_bhkSimpleShapePhantom> },
            { "bhkSphereShape", &construct<bhkSphereShape, RC_bhkSphereShape> },
            { "bhkTransformShape", &construct<bhkConvexTransformShape, RC_bhkConvexTransformShape> },

            // PROPERTIES

            // 4.0.0.2
            { "NiAlphaProperty", &construct<NiAlphaProperty, RC_NiAlphaProperty> },
            { "NiDitherProperty", &construct<NiDitherProperty, RC_NiDitherProperty> },
            { "NiFogProperty", &construct<NiFogProperty, RC_NiFogProperty> },
            { "NiMaterialProperty", &construct<NiMaterialProperty, RC_NiMaterialProperty> },
            { "NiShadeProperty", &construct<NiShadeProperty, RC_NiShadeProperty> },
            { "NiSpecularProperty", &construct<NiSpecularProperty, RC_NiSpecularProperty> },
            { "NiStencilProperty", &construct<NiStencilProperty, RC_NiStencilProperty> },
            { "NiTexturingProperty", &construct<NiTexturingProperty, RC_NiTexturingProperty> },
            { "NiVertexColorProperty", &construct<NiVertexColorProperty, RC_NiVertexColorProperty> },
            { "NiWireframeProperty", &construct<NiWireframeProperty, RC_NiWireframeProperty> },
            { "NiZBufferProperty", &construct<NiZBufferProperty, RC_NiZBufferProperty> },

            // Shader properties, Bethesda
            { "BSShaderProperty", &construct<BSShaderProperty, RC_BSShaderProperty> },
            { "BSShaderPPLightingProperty", &construct<BSShaderPPLightingProperty, RC_BSShaderPPLightingProperty> },
            { "BSShaderNoLightingProperty", &construct<BSShaderNoLightingProperty, RC_BSShaderNoLightingProperty> },
            { "BSLightingShaderProperty", &construct<BSLightingShaderProperty, RC_BSLightingShaderProperty> },
            { "BSEffectShaderProperty", &construct<BSEffectShaderProperty, RC_BSEffectShaderProperty> },
        };
    }

    /// Make the factory map used for parsing the file
    static const std::map<std::string, CreateRecord> factories = makeFactory();

    std::string Reader::versionToString(std::uint32_t version)
    {
        int major = (version >> 24) & 0xFF;
        int minor = (version >> 16) & 0xFF;
        int patch = (version >> 8) & 0xFF;
        int rev = version & 0xFF;

        std::stringstream stream;
        stream << major << "." << minor << "." << patch << "." << rev;
        return stream.str();
    }

    void Reader::parse(Files::IStreamPtr&& stream)
    {
        const std::array<std::uint64_t, 2> fileHash = Files::getHash(mFilename, *stream);
        mHash.append(reinterpret_cast<const char*>(fileHash.data()), fileHash.size() * sizeof(std::uint64_t));

        NIFStream nif(*this, std::move(stream));

        // Check the header string
        std::string head = nif.getVersionString();
        static const std::array<std::string, 2> verStrings = {
            "NetImmerse File Format",
            "Gamebryo File Format",
        };
        const bool supportedHeader = std::any_of(verStrings.begin(), verStrings.end(),
            [&](const std::string& verString) { return head.starts_with(verString); });
        if (!supportedHeader)
            throw Nif::Exception("Invalid NIF header: " + head, mFilename);

        // Get BCD version
        nif.read(mVersion);
        // 4.0.0.0 is an older, practically identical version of the format.
        // It's not used by Morrowind assets but Morrowind supports it.
        static const std::array<uint32_t, 2> supportedVers = {
            NIFStream::generateVersion(4, 0, 0, 0),
            NIFFile::VER_MW,
        };
        const bool supportedVersion
            = std::find(supportedVers.begin(), supportedVers.end(), mVersion) != supportedVers.end();
        const bool writeDebugLog = sWriteNifDebugLog;
        if (!supportedVersion)
        {
            if (!sLoadUnsupportedFiles)
                throw Nif::Exception("Unsupported NIF version: " + versionToString(mVersion), mFilename);
            if (writeDebugLog)
                Log(Debug::Warning) << " NIFFile Warning: Unsupported NIF version: " << versionToString(mVersion)
                                    << ". Proceed with caution! File: " << mFilename;
        }

        const bool hasEndianness = mVersion >= NIFStream::generateVersion(20, 0, 0, 4);
        const bool hasUserVersion = mVersion >= NIFStream::generateVersion(10, 0, 1, 8);
        const bool hasRecTypeListings = mVersion >= NIFStream::generateVersion(5, 0, 0, 1);
        const bool hasRecTypeHashes = mVersion == NIFStream::generateVersion(20, 3, 1, 2);
        const bool hasRecordSizes = mVersion >= NIFStream::generateVersion(20, 2, 0, 5);
        const bool hasGroups = mVersion >= NIFStream::generateVersion(5, 0, 0, 6);
        const bool hasStringTable = mVersion >= NIFStream::generateVersion(20, 1, 0, 1);
        const bool hasRecordSeparators
            = mVersion >= NIFStream::generateVersion(10, 0, 0, 0) && mVersion < NIFStream::generateVersion(10, 2, 0, 0);

        // Record type list
        std::vector<std::string> recTypes;
        // Record type mapping for each record
        std::vector<std::uint16_t> recTypeIndices;

        {
            std::uint8_t endianness = 1;
            if (hasEndianness)
                nif.read(endianness);

            // TODO: find some big-endian files and investigate the difference
            if (endianness == 0)
                throw Nif::Exception("Big endian NIF files are unsupported", mFilename);
        }

        if (hasUserVersion)
            nif.read(mUserVersion);

        mRecords.resize(nif.get<std::uint32_t>());

        // Bethesda stream header
        {
            bool hasBSStreamHeader = false;
            if (mVersion == NIFFile::VER_OB_OLD)
                hasBSStreamHeader = true;
            else if (mUserVersion >= 3 && mVersion >= NIFStream::generateVersion(10, 1, 0, 0))
            {
                if (mVersion <= NIFFile::VER_OB || mVersion == NIFFile::VER_BGS)
                    hasBSStreamHeader = mUserVersion <= 11 || mVersion >= NIFFile::VER_OB;
            }

            if (hasBSStreamHeader)
            {
                nif.read(mBethVersion);
                nif.getExportString(); // Author
                if (mBethVersion >= 131)
                    nif.get<std::uint32_t>(); // Unknown
                else
                    nif.getExportString(); // Process script
                nif.getExportString(); // Export script
                if (mBethVersion >= 103)
                    nif.getExportString(); // Max file path
            }
        }

        if (hasRecTypeListings)
        {
            // TODO: 20.3.1.2 uses DJB hashes instead of strings
            if (hasRecTypeHashes)
                throw Nif::Exception("Hashed record types are unsupported", mFilename);
            else
            {
                nif.getSizedStrings(recTypes, nif.get<std::uint16_t>());
                nif.readVector(recTypeIndices, mRecords.size());
            }
        }

        if (hasRecordSizes) // Record sizes
        {
            std::vector<std::uint32_t> recSizes; // Currently unused
            nif.readVector(recSizes, mRecords.size());
        }

        if (hasStringTable)
        {
            std::uint32_t stringNum, maxStringLength;
            nif.read(stringNum);
            nif.read(maxStringLength);
            nif.getSizedStrings(mStrings, stringNum);
        }

        if (hasGroups)
        {
            std::vector<std::uint32_t> groups; // Currently unused
            nif.readVector(groups, nif.get<std::uint32_t>());
        }

        for (std::size_t i = 0; i < mRecords.size(); i++)
        {
            std::unique_ptr<Record> r;

            std::string rec = hasRecTypeListings ? recTypes[recTypeIndices[i]] : nif.get<std::string>();
            if (rec.empty())
            {
                std::stringstream error;
                error << "Record type is blank (index " << i << ")";
                throw Nif::Exception(error.str(), mFilename);
            }

            // Record separator. Some Havok records in Oblivion do not have it.
            if (hasRecordSeparators && !rec.starts_with("bhk"))
                if (nif.get<int32_t>())
                    Log(Debug::Warning) << "NIFFile Warning: Record of type " << rec << ", index " << i
                                        << " is preceded by a non-zero separator. File: " << mFilename;

            const auto entry = factories.find(rec);

            if (entry == factories.end())
                throw Nif::Exception("Unknown record type " + rec, mFilename);

            r = entry->second();

            if (!supportedVersion && writeDebugLog)
                Log(Debug::Verbose) << "NIF Debug: Reading record of type " << rec << ", index " << i << " ("
                                    << mFilename << ")";

            assert(r != nullptr);
            assert(r->recType != RC_MISSING);
            r->recName = rec;
            r->recIndex = i;
            r->read(&nif);
            mRecords[i] = std::move(r);
        }

        // Determine which records are roots
        mRoots.resize(nif.get<uint32_t>());
        for (std::size_t i = 0; i < mRoots.size(); i++)
        {
            std::int32_t idx;
            nif.read(idx);
            if (idx >= 0 && static_cast<std::size_t>(idx) < mRecords.size())
            {
                mRoots[i] = mRecords[idx].get();
            }
            else
            {
                mRoots[i] = nullptr;
                Log(Debug::Warning) << "NIFFile Warning: Root " << i + 1 << " does not point to a record: index " << idx
                                    << ". File: " << mFilename;
            }
        }

        // Once parsing is done, do post-processing.
        for (const auto& record : mRecords)
            record->post(*this);
    }

    void Reader::setUseSkinning(bool skinning)
    {
        mUseSkinning = skinning;
    }

    std::atomic_bool Reader::sLoadUnsupportedFiles = false;
    std::atomic_bool Reader::sWriteNifDebugLog = false;

    void Reader::setLoadUnsupportedFiles(bool load)
    {
        sLoadUnsupportedFiles = load;
    }

    void Reader::setWriteNifDebugLog(bool value)
    {
        sWriteNifDebugLog = value;
    }

    std::string Reader::getString(std::uint32_t index) const
    {
        if (index == std::numeric_limits<std::uint32_t>::max())
            return std::string();
        return mStrings.at(index);
    }

}
