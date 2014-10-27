/****************************************************************************
**
** Copyright (C) 2013 Digia Plc
** All rights reserved.
** For any questions to Digia, please use contact form at http://qt.digia.com <http://qt.digia.com/>
**
** This file is part of the Qt Enterprise Qt Quick Profiler Add-on.
**
** Licensees holding valid Qt Enterprise licenses may use this file in
** accordance with the Qt Enterprise License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.
**
** If you have questions regarding the use of this file, please use
** contact form at http://qt.digia.com <http://qt.digia.com/>
**
****************************************************************************/

#include "scenegraphtimelinemodel.h"
#include "qmldebug/qmlprofilereventtypes.h"
#include "qmlprofiler/qmlprofilermodelmanager.h"

#include <QCoreApplication>
#include <QDebug>

namespace QmlProfilerExtension {
namespace Internal {

using namespace QmlProfiler;

static const char *ThreadLabels[] = {
    QT_TRANSLATE_NOOP("MainView", "GUI Thread"),
    QT_TRANSLATE_NOOP("MainView", "Render Thread"),
    QT_TRANSLATE_NOOP("MainView", "Render Thread Details")
};

static const char *StageLabels[] = {
    QT_TRANSLATE_NOOP("MainView", "Polish"),
    QT_TRANSLATE_NOOP("MainView", "Wait"),
    QT_TRANSLATE_NOOP("MainView", "GUI Thread Sync"),
    QT_TRANSLATE_NOOP("MainView", "Animations"),
    QT_TRANSLATE_NOOP("MainView", "Render Thread Sync"),
    QT_TRANSLATE_NOOP("MainView", "Render"),
    QT_TRANSLATE_NOOP("MainView", "Swap"),
    QT_TRANSLATE_NOOP("MainView", "Render Preprocess"),
    QT_TRANSLATE_NOOP("MainView", "Render Update"),
    QT_TRANSLATE_NOOP("MainView", "Render Bind"),
    QT_TRANSLATE_NOOP("MainView", "Render Render"),
    QT_TRANSLATE_NOOP("MainView", "Material Compile"),
    QT_TRANSLATE_NOOP("MainView", "Glyph Render"),
    QT_TRANSLATE_NOOP("MainView", "Glyph Upload"),
    QT_TRANSLATE_NOOP("MainView", "Texture Bind"),
    QT_TRANSLATE_NOOP("MainView", "Texture Convert"),
    QT_TRANSLATE_NOOP("MainView", "Texture Swizzle"),
    QT_TRANSLATE_NOOP("MainView", "Texture Upload"),
    QT_TRANSLATE_NOOP("MainView", "Texture Mipmap"),
    QT_TRANSLATE_NOOP("MainView", "Texture Delete")
};

enum SceneGraphCategoryType {
    SceneGraphGUIThread,
    SceneGraphRenderThread,
    SceneGraphRenderThreadDetails,

    MaximumSceneGraphCategoryType
};

Q_STATIC_ASSERT(sizeof(StageLabels) ==
                SceneGraphTimelineModel::MaximumSceneGraphStage * sizeof(const char *));

SceneGraphTimelineModel::SceneGraphTimelineModel(QObject *parent)
    : AbstractTimelineModel(tr(QmlProfilerModelManager::featureName(QmlDebug::ProfileSceneGraph)),
                            QmlDebug::SceneGraphFrame, QmlDebug::MaximumRangeType, parent)
{
}

quint64 SceneGraphTimelineModel::features() const
{
    return 1 << QmlDebug::ProfileSceneGraph;
}

int SceneGraphTimelineModel::row(int index) const
{
    return expanded() ? (m_data[index].stage + 1) : m_data[index].rowNumberCollapsed;
}

int SceneGraphTimelineModel::selectionId(int index) const
{
    return m_data[index].stage;
}

QColor SceneGraphTimelineModel::color(int index) const
{
    return colorBySelectionId(index);
}

QVariantList SceneGraphTimelineModel::labels() const
{
    QVariantList result;

    if (expanded() && !hidden() && !isEmpty()) {
        for (SceneGraphStage i = MinimumSceneGraphStage; i < MaximumSceneGraphStage;
             i = static_cast<SceneGraphStage>(i + 1)) {
            QVariantMap element;
            element.insert(QLatin1String("displayName"), tr(threadLabel(i)));
            element.insert(QLatin1String("description"), tr(StageLabels[i]));
            element.insert(QLatin1String("id"), i);
            result << element;
        }
    }

    return result;
}

QVariantMap SceneGraphTimelineModel::details(int index) const
{
    QVariantMap result;
    const SceneGraphEvent *ev = &m_data[index];

    result.insert(QLatin1String("displayName"),
                  tr(threadLabel(static_cast<SceneGraphStage>(ev->stage))));
    result.insert(tr("Stage"), tr(StageLabels[ev->stage]));
    result.insert(tr("Duration"), QmlProfilerBaseModel::formatTime(duration(index)));
    if (ev->glyphCount >= 0)
        result.insert(tr("Glyphs"), QString::number(ev->glyphCount));

    return result;
}

void SceneGraphTimelineModel::loadData()
{
    clear();
    QmlProfilerDataModel *simpleModel = modelManager()->qmlModel();
    if (simpleModel->isEmpty())
        return;

    // combine the data of several eventtypes into two rows
    const QVector<QmlProfilerDataModel::QmlEventTypeData> &types = simpleModel->getEventTypes();
    foreach (const QmlProfilerDataModel::QmlEventData &event, simpleModel->getEvents()) {
        const QmlProfilerDataModel::QmlEventTypeData &type = types[event.typeIndex];
        if (!accepted(type))
            continue;

        switch ((QmlDebug::SceneGraphFrameType)type.detailType) {
        case QmlDebug::SceneGraphRendererFrame: {
            // Breakdown of render times. We repeat "render" here as "net" render time. It would
            // look incomplete if that was left out as the printf profiler lists it, too, and people
            // are apparently comparing that. Unfortunately it is somewhat redundant as the other
            // parts of the breakdown are usually very short.
            qint64 startTime = event.startTime - event.numericData1 - event.numericData2 -
                    event.numericData3 - event.numericData4;
            startTime += insert(startTime, event.numericData1, event.typeIndex, RenderPreprocess);
            startTime += insert(startTime, event.numericData2, event.typeIndex, RenderUpdate);
            startTime += insert(startTime, event.numericData3, event.typeIndex, RenderBind);
            insert(startTime, event.numericData4, event.typeIndex, RenderRender);
            break;
        }
        case QmlDebug::SceneGraphAdaptationLayerFrame: {
            qint64 startTime = event.startTime - event.numericData2 - event.numericData3;
            startTime += insert(startTime, event.numericData2, event.typeIndex, GlyphRender,
                                event.numericData1);
            insert(startTime, event.numericData3, event.typeIndex, GlyphStore, event.numericData1);
            break;
        }
        case QmlDebug::SceneGraphContextFrame: {
            insert(event.startTime - event.numericData1, event.numericData1, event.typeIndex,
                      Material);
            break;
        }
        case QmlDebug::SceneGraphRenderLoopFrame: {
            qint64 startTime = event.startTime - event.numericData1 - event.numericData2 -
                    event.numericData3;
            startTime += insert(startTime, event.numericData1, event.typeIndex,
                                   RenderThreadSync);
            startTime += insert(startTime, event.numericData2, event.typeIndex,
                                   Render);
            insert(startTime, event.numericData3, event.typeIndex, Swap);
            break;
        }
        case QmlDebug::SceneGraphTexturePrepare: {
            qint64 startTime = event.startTime - event.numericData1 - event.numericData2 -
                    event.numericData3 - event.numericData4 - event.numericData5;
            startTime += insert(startTime, event.numericData1, event.typeIndex, TextureBind);
            startTime += insert(startTime, event.numericData2, event.typeIndex, TextureConvert);
            startTime += insert(startTime, event.numericData3, event.typeIndex, TextureSwizzle);
            startTime += insert(startTime, event.numericData4, event.typeIndex, TextureUpload);
            insert(startTime, event.numericData5, event.typeIndex, TextureMipmap);
            break;
        }
        case QmlDebug::SceneGraphTextureDeletion: {
            insert(event.startTime - event.numericData1, event.numericData1, event.typeIndex,
                   TextureDeletion);
            break;
        }
        case QmlDebug::SceneGraphPolishAndSync: {
            qint64 startTime = event.startTime - event.numericData1 - event.numericData2 -
                    event.numericData3 - event.numericData4;

            startTime += insert(startTime, event.numericData1, event.typeIndex, Polish);
            startTime += insert(startTime, event.numericData2, event.typeIndex, Wait);
            startTime += insert(startTime, event.numericData3, event.typeIndex, GUIThreadSync);
            insert(startTime, event.numericData4, event.typeIndex, Animations);
            break;
        }
        case QmlDebug::SceneGraphWindowsAnimations: {
            // GUI thread, separate animations stage
            insert(event.startTime - event.numericData1, event.numericData1, event.typeIndex,
                   Animations);
            break;
        }
        case QmlDebug::SceneGraphPolishFrame: {
            // GUI thread, separate polish stage
            insert(event.startTime - event.numericData1, event.numericData1, event.typeIndex,
                   Polish);
            break;
        }
        default: break;
        }

        updateProgress(count(), simpleModel->getEvents().count());
    }

    computeNesting();
    flattenLoads();
    updateProgress(1, 1);
}

void SceneGraphTimelineModel::flattenLoads()
{
    int collapsedRowCount = 0;

    // computes "compressed row"
    QVector <qint64> eventEndTimes;

    for (int i = 0; i < count(); i++) {
        SceneGraphEvent &event = m_data[i];
        // Don't try to put render thread events in GUI row and vice versa.
        // Rows below those are free for all.
        if (event.stage < MaximumGUIThreadStage)
            event.rowNumberCollapsed = SceneGraphGUIThread;
        else if (event.stage < MaximumRenderThreadStage)
            event.rowNumberCollapsed = SceneGraphRenderThread;
        else
            event.rowNumberCollapsed = SceneGraphRenderThreadDetails;

        while (eventEndTimes.count() > event.rowNumberCollapsed &&
               eventEndTimes[event.rowNumberCollapsed] > startTime(i))
            ++event.rowNumberCollapsed;

        while (eventEndTimes.count() <= event.rowNumberCollapsed)
            eventEndTimes << 0; // increase stack length, proper value added below
        eventEndTimes[event.rowNumberCollapsed] = endTime(i);

        // readjust to account for category empty row
        event.rowNumberCollapsed++;
        if (event.rowNumberCollapsed > collapsedRowCount)
            collapsedRowCount = event.rowNumberCollapsed;
    }

    // Starting from 0, count is maxIndex+1
    setCollapsedRowCount(collapsedRowCount + 1);
    setExpandedRowCount(MaximumSceneGraphStage + 1);
}

/*!
 * Inserts an event characterized by \a start time, \a duration, \a typeIndex, \a stage and possibly
 * \a glyphCount (if it's a \c GlyphRender or \c GlyphStore event) into the scene graph model if its
 * \a duration is greater than 0. Returns \a duration in that case; otherwise returns 0.
 */
qint64 SceneGraphTimelineModel::insert(qint64 start, qint64 duration, int typeIndex,
                                       SceneGraphStage stage, int glyphCount)
{
    if (duration <= 0)
        return 0;

    m_data.insert(AbstractTimelineModel::insert(start, duration, typeIndex),
                SceneGraphEvent(stage, glyphCount));
    return duration;
}

const char *SceneGraphTimelineModel::threadLabel(SceneGraphStage stage)
{
    if (stage < MaximumGUIThreadStage)
        return ThreadLabels[SceneGraphGUIThread];
    else if (stage < MaximumRenderThreadStage)
        return ThreadLabels[SceneGraphRenderThread];
    else
        return ThreadLabels[SceneGraphRenderThreadDetails];

}

void SceneGraphTimelineModel::clear()
{
    m_data.clear();
    AbstractTimelineModel::clear();
}

SceneGraphTimelineModel::SceneGraphEvent::SceneGraphEvent(SceneGraphStage stage, int glyphCount) :
    stage(stage), rowNumberCollapsed(-1), glyphCount(glyphCount)
{
}

} // namespace Internal
} // namespace QmlProfilerExtension
