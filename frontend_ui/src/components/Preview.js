import React, {Component} from 'react';
import Grid from '@material-ui/core/Grid';
import Slider from '@material-ui/core/Slider';
import Switch from '@material-ui/core/Switch';
import Typography from '@material-ui/core/Typography';
import Paper from '@material-ui/core/Paper';
import PinchZoomPan from "react-responsive-pinch-zoom-pan";
import SpotFindingPlot from './SpotFindingPlot.js';

class Preview extends Component {
  state = {
    contrast: 10.0,
    image: 0,
    logarithmic: true,
  }

  handleChange = (event) => {
    this.setState({[event.target.name]: event.target.checked });
  };

  componentDidMount() {
    this.interval = setInterval(() => this.updateImg(), 2000);
  }

  componentWillUnmount() {
    clearInterval(this.interval);
  }

  updateImg() {
    this.setState({image: (this.state.image + 1) % 256000});
  }

  sliderMoved = (event, newValue) => {
    this.setState({contrast: newValue});
  };

  render() {
    return <div style={{ margin: 'auto' }}>

      <Grid container spacing={3}>
        <Grid item xs={1} />
        <Grid item xs={6}>
          <Paper>
            <div style={{  maxWidth: '1030px', maxHeight: '1028px' }}>
              <PinchZoomPan maxScale={4.0}>
                {this.state.logarithmic?
                    <img src={"http://mx-jungfrau-1:5232/preview_log/" + this.state.contrast + "/" + this.state.image} alt="Preview"/>:
                    <img src={"http://mx-jungfrau-1:5232/preview/" + this.state.contrast + "/" + this.state.image} alt="Preview"/>}
              </PinchZoomPan>
            </div>
          </Paper>
        </Grid>
        <Grid item xs={4}>
          <Paper>
            <Typography> Contrast </Typography>
            <Slider defaultValue={10.0} min={1.0} max={200} onChange={this.sliderMoved} valueLabelDisplay="auto" style= {{maxWidth: 300}} />
            <Typography> Log scale </Typography>
            <Switch checked={this.state.logarithmic} onChange={this.handleChange} name="logarithmic" />
            <Typography> Spot count vs. rotation angle </Typography>
          </Paper>
        </Grid>
        <Grid item xs={1}/>
      </Grid>
    </div>;
  }
}

export default Preview;
