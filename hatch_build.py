"""Hatchling build hook that compiles the Meson project and packages the plugin."""

from __future__ import annotations

from pathlib import Path
from typing import Any, override
import os
import shlex
import shutil
import subprocess as sp
import sys

from hatchling.builders.hooks.plugin.interface import BuildHookInterface
from packaging import tags

__all__ = ('CustomHook',)

PLUGIN_SUFFIXES = ('.dll', '.dylib', '.so')
"""Shared-library suffixes copied into the wheel.

:meta hide-value:
"""


# NOTE: Deliberately not subscripted (e.g. ``BuildHookInterface[Any]``). Hatchling 1.32.3
# briefly shipped the interface with two type parameters, which makes any explicit
# subscription a runtime ``TypeError`` on some versions. The parameters are typing-only.
class CustomHook(BuildHookInterface):  # type: ignore[type-arg]
    """Compile the plugin with Meson and stage it for the wheel."""

    source_dir = Path('build-wheel')
    """Meson build directory, kept separate from a developer's own ``build``."""
    target_dir = Path('vapoursynth') / 'plugins'
    """Directory the plugin is staged in, mirroring its location in ``site-packages``."""

    @override
    def initialize(self, version: str, build_data: dict[str, Any]) -> None:
        """
        Build the plugin and copy it next to the VapourSynth package.

        VapourSynth recursively autoloads every native plugin under
        ``<site-packages>/vapoursynth/plugins``, so installing the wheel is all
        that is needed to make ``esrgan`` available.

        Parameters
        ----------
        version : str
            Build target version. Unused.
        build_data : dict[str, Any]
            Build metadata consumed by the wheel builder.
        """
        build_data['pure_python'] = False
        # The plugin is loaded by VapourSynth itself rather than by CPython, so
        # it depends only on the operating system and architecture. Tag the
        # wheel accordingly instead of inferring an interpreter-specific tag,
        # which would require one wheel per Python version.
        platform_tag = os.environ.get('REALESRGAN_WHEEL_PLATFORM_TAG') or self._platform_tag()
        build_data['tag'] = f'py3-none-{platform_tag}'
        meson = (sys.executable, '-m', 'mesonbuild.mesonmain')
        # Extra arguments for ``meson setup``, taken from REALESRGAN_MESON_SETUP_ARGS
        # split like a shell (e.g. '--buildtype release -Db_vscrt=mt'). Used by CI
        # to build optimised wheels; local builds keep Meson's defaults.
        setup_args = shlex.split(os.environ.get('REALESRGAN_MESON_SETUP_ARGS', ''))
        setup = [*meson, 'setup', str(self.source_dir), *setup_args]
        if (self.source_dir / 'meson-info').is_dir():
            # ``--vsenv`` is read-only after the first configure, so it may only
            # be passed when the build directory is created.
            setup.append('--reconfigure')
        else:
            # Activate the Visual Studio environment on Windows, where MSVC is
            # required. Ignored elsewhere.
            setup.append('--vsenv')
        sp.run(setup, check=True)
        sp.run([*meson, 'compile', '-C', str(self.source_dir)], check=True)
        self.target_dir.mkdir(parents=True, exist_ok=True)
        for path in (self.source_dir / 'src').glob('*'):
            if path.is_file() and path.suffix in PLUGIN_SUFFIXES:
                shutil.copy2(path, self.target_dir)

    @override
    def finalize(self, version: str, build_data: dict[str, Any], artifact_path: str) -> None:
        """
        Remove the staged plugin tree once the wheel has been written.

        Parameters
        ----------
        version : str
            Build target version. Unused.
        build_data : dict[str, Any]
            Build metadata. Unused.
        artifact_path : str
            Path of the built wheel. Unused.
        """
        shutil.rmtree(self.target_dir.parent, ignore_errors=True)

    @staticmethod
    def _platform_tag() -> str:
        """
        Work out the platform tag the wheel should carry.

        On macOS :py:func:`packaging.tags.platform_tags` reports the version of
        the machine doing the build, which would tag the wheel as requiring
        whatever the runner happens to run and hide it from everyone on an
        older release. Honour ``MACOSX_DEPLOYMENT_TARGET``, which is what the
        plugin is actually built against, when it is set.

        Returns
        -------
        str
            Platform tag, for example ``macosx_11_0_arm64``.
        """
        if sys.platform == 'darwin' and (target := os.environ.get('MACOSX_DEPLOYMENT_TARGET')):
            major, _, minor = target.partition('.')
            return next(iter(tags.mac_platforms(version=(int(major), int(minor or 0)))))
        return next(tags.platform_tags())
