"""
Foong Min Wong
University of Wisconsin, Eau Claire
Fall 2018 - Spring 2019
Raw and smooth stereolithography (STL) surface export & solidify feature for Bertini_real

.. module:: tmesh
    :platform: Unix, Windows
    :synopsis: The tmesh uses Trimesh to export and solidify raw/smooth STL and normal fixing.

"""
import bertini_real
import copy
import math
import numpy as np
import os
import trimesh


class ReversableList(list):
    """ Create a ReversableList object for reversing order of data 

        Args:
            list: The list to be read.
    """

    def reverse(self):
        """ Reverse function for raw surface data

        Returns A reversed list
        """
        return list(reversed(self))


class TMesh():
    """ Create a TMesh object for exporting STL files """

    def __init__(self, data=None):
        """ Read data from disk

            Args:
                data: surface decomposition data

        """

        if data is None:
            self.decomposition = bertini_real.data.ReadMostRecent()
        else:
            self.decomposition = data

    def stl_raw(self):
        """ Export raw decomposition of surfaces to STL """

        print('\n' + '\x1b[0;34;40m' +
              'Generating raw STL surface...' + '\x1b[0m')

        points = extract_points(self)

        surf = self.decomposition.surface

        num_faces = surf.num_faces

        which_faces = list(range(num_faces))

        if not len(which_faces):
            which_faces = list(range(num_faces))

        num_total_faces = 0
        for ii in range(len(which_faces)):
            curr_face = surf.faces[which_faces[ii]]
            num_total_faces = num_total_faces + 2 * \
                (curr_face['num left'] + curr_face['num right'] + 2)
        num_total_faces = num_total_faces * 2

        total_face_index = 0
        TT = []

        for cc in range(len(which_faces)):
            ii = which_faces[cc]
            face = surf.faces[ii]

            if (face['middle slice index']) == -1:
                continue

            case = 1
            left_edge_counter = 0
            right_edge_counter = 0

            T = []

            while 1:
                ## top edge ##
                if case == 1:

                    case += 1
                    if face['top'] < 0:
                        continue

                    curr_edge = -10
                    if(face['system top'] == 'input_critical_curve'):
                        curr_edge = surf.critical_curve.edges[face['top']]
                    elif(face['system top'] == 'input_surf_sphere'):
                        curr_edge = surf.sphere_curve.edges[face['top']]
                    else:
                        for zz in range(len(surf.singular_curves)):
                            if(surf.singular_names[zz] == face['system top']):
                                curr_edge = surf.singular_curves[
                                    zz].edges[face['top']]

                    if(curr_edge == -10):
                        continue

                    if (curr_edge[0] < 0 and curr_edge[1] < 0 and curr_edge[2] < 0):
                        continue

                    curr_edge = ReversableList(curr_edge)
                    curr_edge = curr_edge.reverse()

                ## bottom edge ##
                elif case == 2:

                    case += 1

                    if face['bottom'] < 0:
                        continue

                    curr_edge = -10
                    if(face['system bottom'] == 'input_critical_curve'):
                        curr_edge = surf.critical_curve.edges[face['bottom']]
                    elif(face['system bottom'] == 'input_surf_sphere'):
                        curr_edge = surf.sphere_curve.edges[face['bottom']]
                    else:
                        for zz in range(len(surf.singular_curves)):
                            if(surf.singular_names[zz] == face['system bottom']):
                                curr_edge = surf.singular_curves[
                                    zz].edges[face['bottom']]

                    if(curr_edge == -10):
                        continue

                    if (curr_edge[0] < 0 and curr_edge[1] < 0 and curr_edge[2] < 0):
                        continue

                ## left edge ##
                elif case == 3:

                    if left_edge_counter < face['num left']:

                        if face['left'][left_edge_counter] < 0:
                            continue

                        slice_ind = face['middle slice index']
                        edge_ind = face['left'][left_edge_counter]

                        curr_edge = surf.critical_point_slices[
                            slice_ind].edges[edge_ind]
                        left_edge_counter = left_edge_counter + 1  # increment

                    else:
                        case = case + 1
                        continue

                ## right edge ##
                elif case == 4:

                    if right_edge_counter < face['num right']:

                        if face['right'][right_edge_counter] < 0:
                            continue

                        slice_ind = face['middle slice index'] + 1
                        edge_ind = face['right'][right_edge_counter]
                        curr_edge = surf.critical_point_slices[
                            slice_ind].edges[edge_ind]
                        right_edge_counter = right_edge_counter + 1

                        curr_edge = ReversableList(curr_edge)
                        curr_edge = curr_edge.reverse()

                    else:
                        case += 1
                        continue

                ## last case ##
                elif case == 5:
                    break

                t1 = [points[curr_edge[0]], points[curr_edge[1]],
                      points[face['midpoint']]]
                t2 = [points[curr_edge[1]], points[curr_edge[2]],
                      points[face['midpoint']]]

                t3 = (curr_edge[0], curr_edge[1], face['midpoint'])
                t4 = (curr_edge[1], curr_edge[2], face['midpoint'])

                T.append(t1)
                T.append(t2)

                TT.append(t3)
                TT.append(t4)

        faces = [TT]
        vertex = []

        for p in points:
            vertex.append(p)

        vertex_np_array = np.array(vertex)
        face = []

        for f in faces:
            for tri in f:
                face.append([tri[0], tri[1], tri[2]])

        face_np_array = np.array(face)

        raw_mesh = trimesh.Trimesh(vertex_np_array, face_np_array)

        fileName = os.getcwd().split(os.sep)[-1]

        raw_mesh.fix_normals()

        raw_mesh.export(file_obj='stl_raw_' + fileName +
                        '.stl', file_type='stl')

        print("Export " + '\x1b[0;35;40m' + "stl_raw_" +
              fileName + ".stl" + '\x1b[0m' + " successfully")

    def stl_smooth(self):
        """ Export smooth decomposition of surfaces to STL """

        print('\n' + '\x1b[0;34;40m' +
              'Generating smooth STL surface...' + '\x1b[0m')

        points = extract_points(self)

        faces = self.decomposition.surface.surface_sampler_data

        vertex = []

        for p in points:
            vertex.append(p)

        vertex_np_array = np.array(vertex)

        face = []

        for f in faces:
            for tri in f:
                face.append([tri[0], tri[1], tri[2]])

        face_np_array = np.array(face)

        A = trimesh.Trimesh(vertex_np_array, face_np_array)

        fileName = os.getcwd().split(os.sep)[-1]

        A.fix_normals()

        A.export(file_obj='stl_smooth_' +
                 fileName + '.stl', file_type='stl')

        print("Export " + '\x1b[0;35;40m' + "stl_smooth_" +
              fileName + ".stl" + '\x1b[0m' + " successfully")

    def solidify_raw(self):
        """ Solidify raw version of STL """

        print('\n' + '\x1b[0;34;40m' +
              'Solidiying raw STL surface...' + '\x1b[0m')

        points = extract_points(self)

        surf = self.decomposition.surface

        num_faces = surf.num_faces

        which_faces = list(range(num_faces))

        if not len(which_faces):
            which_faces = list(range(num_faces))

        num_total_faces = 0
        for ii in range(len(which_faces)):
            curr_face = surf.faces[which_faces[ii]]
            num_total_faces = num_total_faces + 2 * \
                (curr_face['num left'] + curr_face['num right'] + 2)
        num_total_faces = num_total_faces * 2

        total_face_index = 0
        TT = []

        for cc in range(len(which_faces)):
            ii = which_faces[cc]
            face = surf.faces[ii]

            if (face['middle slice index']) == -1:
                continue

            case = 1
            left_edge_counter = 0
            right_edge_counter = 0

            T = []

            while 1:
                ## top edge ##
                if case == 1:

                    case += 1
                    if face['top'] < 0:
                        continue

                    curr_edge = -10
                    if(face['system top'] == 'input_critical_curve'):
                        curr_edge = surf.critical_curve.edges[face['top']]
                    elif(face['system top'] == 'input_surf_sphere'):
                        curr_edge = surf.sphere_curve.edges[face['top']]
                    else:
                        for zz in range(len(surf.singular_curves)):
                            if(surf.singular_names[zz] == face['system top']):
                                curr_edge = surf.singular_curves[
                                    zz].edges[face['top']]

                    if(curr_edge == -10):
                        continue

                    if (curr_edge[0] < 0 and curr_edge[1] < 0 and curr_edge[2] < 0):
                        continue

                    curr_edge = ReversableList(curr_edge)
                    curr_edge = curr_edge.reverse()

                ## bottom edge ##
                elif case == 2:

                    case += 1

                    if face['bottom'] < 0:
                        continue

                    curr_edge = -10
                    if(face['system bottom'] == 'input_critical_curve'):
                        curr_edge = surf.critical_curve.edges[face['bottom']]
                    elif(face['system bottom'] == 'input_surf_sphere'):
                        curr_edge = surf.sphere_curve.edges[face['bottom']]
                    else:
                        for zz in range(len(surf.singular_curves)):
                            if(surf.singular_names[zz] == face['system bottom']):
                                curr_edge = surf.singular_curves[
                                    zz].edges[face['bottom']]

                    if(curr_edge == -10):
                        continue

                    if (curr_edge[0] < 0 and curr_edge[1] < 0 and curr_edge[2] < 0):
                        continue

                ## left edge ##
                elif case == 3:

                    if left_edge_counter < face['num left']:

                        if face['left'][left_edge_counter] < 0:
                            continue

                        slice_ind = face['middle slice index']
                        edge_ind = face['left'][left_edge_counter]

                        curr_edge = surf.critical_point_slices[
                            slice_ind].edges[edge_ind]
                        left_edge_counter = left_edge_counter + 1  # increment

                    else:
                        case = case + 1
                        continue

                ## right edge ##
                elif case == 4:

                    if right_edge_counter < face['num right']:

                        if face['right'][right_edge_counter] < 0:
                            continue

                        slice_ind = face['middle slice index'] + 1
                        edge_ind = face['right'][right_edge_counter]
                        curr_edge = surf.critical_point_slices[
                            slice_ind].edges[edge_ind]
                        right_edge_counter = right_edge_counter + 1

                        curr_edge = ReversableList(curr_edge)
                        curr_edge = curr_edge.reverse()

                    else:
                        case += 1
                        continue

                ## last case ##
                elif case == 5:
                    break

                t1 = [points[curr_edge[0]], points[curr_edge[1]],
                      points[face['midpoint']]]
                t2 = [points[curr_edge[1]], points[curr_edge[2]],
                      points[face['midpoint']]]

                t3 = (curr_edge[0], curr_edge[1], face['midpoint'])
                t4 = (curr_edge[1], curr_edge[2], face['midpoint'])

                T.append(t1)
                T.append(t2)

                TT.append(t3)
                TT.append(t4)

        faces = [TT]
        vertex = []

        for p in points:
            vertex.append(p)

        vertex_np_array = np.array(vertex)
        face = []

        for f in faces:
            for tri in f:
                face.append([tri[0], tri[1], tri[2]])

        face_np_array = np.array(face)

        A = trimesh.Trimesh(vertex_np_array, face_np_array)

        A.fix_normals()

        B = copy.deepcopy(A)

        # reverse every triangles and flip every normals
        B.invert()

        # calculate A, B vertex normals
        vertexnormsA = A.vertex_normals
        vertexnormsB = B.vertex_normals

        offset = 0
        total = 0.1

        distA = (total) * (offset + 1) / 2
        distB = (total) * (1 - (offset + 1) / 2)

        # create A & B vertices that move corresponding to vertex normals and
        # distance
        A.vertices = [v + vn * distA for v,
                      vn in zip(A.vertices, A.vertex_normals)]
        B.vertices = [v + vn * distB for v,
                      vn in zip(B.vertices, B.vertex_normals)]

        numVerts = len(A.vertices)

        T = []

        boundary_groups = trimesh.grouping.group_rows(
            A.edges_sorted, require_count=1)

        boundary_edges = A.edges[boundary_groups]

        for edge in boundary_edges:
            for i in range(len(edge) - 1):
                t1 = [edge[i], edge[i + 1], edge[i] + numVerts]
                t2 = [edge[i + 1], edge[i] + numVerts, edge[i + 1] + numVerts]

                T.append(t1)
                T.append(t2)

        Q = np.concatenate((A.vertices, B.vertices), axis=0)

        newBoundary = trimesh.Trimesh(Q, T)

        finalmesh = A + newBoundary + B

        finalmesh.fix_normals()

        fileName = os.getcwd().split(os.sep)[-1]

        finalmesh.export(file_obj='solidify_raw_' +
                         fileName + '.stl', file_type='stl')

        print("Export " + '\x1b[0;35;40m' + "solidify_raw_" +
              fileName + ".stl" + '\x1b[0m' + " successfully")

    def solidify_smooth(self):
        """ Solidify smooth version of STL """

        print('\n' + '\x1b[0;34;40m' +
              'Solidiying smooth STL surface...' + '\x1b[0m')

        points = extract_points(self)

        faces = self.decomposition.surface.surface_sampler_data

        vertex = []

        for p in points:
            vertex.append(p)

        vertex_np_array = np.array(vertex)

        face = []

        for f in faces:
            for tri in f:
                face.append([tri[0], tri[1], tri[2]])

        face_np_array = np.array(face)

        A = trimesh.Trimesh(vertex_np_array, face_np_array)

        A.fix_normals()

        B = copy.deepcopy(A)

        # reverse every triangles and flip every normals
        B.invert()

        # calculate A, B vertex normals
        vertexnormsA = A.vertex_normals
        vertexnormsB = B.vertex_normals

        offset = 0
        total = 0.1

        distA = (total) * (offset + 1) / 2
        distB = (total) * (1 - (offset + 1) / 2)

        # create A & B vertices that move corresponding to vertex normals and
        # distance
        A.vertices = [v + vn * distA for v,
                      vn in zip(A.vertices, A.vertex_normals)]
        B.vertices = [v + vn * distB for v,
                      vn in zip(B.vertices, B.vertex_normals)]

        numVerts = len(A.vertices)

        T = []

        boundary_groups = trimesh.grouping.group_rows(
            A.edges_sorted, require_count=1)

        boundary_edges = A.edges[boundary_groups]

        for edge in boundary_edges:
            for i in range(len(edge) - 1):
                t1 = [edge[i], edge[i + 1], edge[i] + numVerts]
                t2 = [edge[i + 1], edge[i] + numVerts, edge[i + 1] + numVerts]

                T.append(t1)
                T.append(t2)

        Q = np.concatenate((A.vertices, B.vertices), axis=0)

        newBoundary = trimesh.Trimesh(Q, T)

        finalmesh = A + newBoundary + B

        finalmesh.fix_normals()

        fileName = os.getcwd().split(os.sep)[-1]

        finalmesh.export(file_obj='solidify_smooth_' +
                         fileName + '.stl', file_type='stl')

        print("Export " + '\x1b[0;35;40m' + "solidify_smooth_" +
              fileName + ".stl" + '\x1b[0m' + " successfully")


def extract_points(self):
    """ Helper method for plot_surface_samples()
        Extract points from vertices

        :param data: The decomposition that we are rendering.
        :rtype: List of tuples of length 3.
    """

    points = []

    for v in self.decomposition.vertices:
        q = [None] * 3

        for i in range(3):
            q[i] = v['point'][i].real
        points.append(q)

    return points


def stl_raw(data=None):
    """ Create a TMesh object and export raw surface STL

       :param data: The decomposition that we are rendering.
    """

    surface = TMesh(data)
    surface.stl_raw()


def stl_smooth(data=None):
    """ Create a TMesh object and export smooth surface STL

        :param data: The decomposition that we are rendering.
    """

    surface = TMesh(data)
    surface.stl_smooth()


def solidify_raw(data=None):
    """ Create a TMesh object and solidify raw surface STL

        :param data: The decomposition that we are rendering.
    """

    surface = TMesh(data)
    surface.solidify_raw()


def solidify_smooth(data=None):
    """ Create a TMesh object and solidify smooth surface STL

        :param data: The decomposition that we are rendering.
    """

    surface = TMesh(data)
    surface.solidify_smooth()


# def solidify(data=None, totalDist=0.1, offset=0):
#     """ Create a TMesh object and solidify objects """
#     surface = TMesh(data)
#     surface.solidify(totalDist, offset)
