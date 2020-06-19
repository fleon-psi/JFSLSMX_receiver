import React, {Component} from 'react';
import Grid from '@material-ui/core/Grid';
import Slider from '@material-ui/core/Slider';
import Switch from '@material-ui/core/Switch';
import PinchZoomPan from "react-responsive-pinch-zoom-pan";
import SpotFindingPlot from './SpotFindingPlot.js';

class Preview extends Component {
  state = {
     contrast: 10.0,
     image: 0,
     logarithmic: false,
  }

  handleChange = (event) => {
    this.setState({[event.target.name]: event.target.checked });
  };

  componentDidMount() {
    this.interval = setInterval(() => this.updateImg(), 500);
  }

  componentWillUnmount() {
    clearInterval(this.interval);
  }

  updateImg() {
    this.setState({image: this.state.image + 1});
  }

  sliderMoved = (event, newValue) => {
    this.setState({contrast: newValue});
  };

  render() {
     return <div style={{ margin: 'auto' }}>
        <Grid container spacing={3}>
          <Grid item xs={7}>
            <div style={{  width: '1030px', height: '1028px' }}>
              <PinchZoomPan maxScale={4.0}>
                {this.state.logarithmic?
                   <img src={"http://mx-jungfrau-1:5232/preview_log/" + this.state.contrast + "/" + this.state.image} alt="Preview"/>:
                   <img src={"http://mx-jungfrau-1:5232/preview/" + this.state.contrast + "/" + this.state.image} alt="Preview"/>}
              </PinchZoomPan>
            </div>
          </Grid>
          <Grid item xs={5}>
            <Slider defaultValue={10.0} min={1.0} max={200} onChange={this.sliderMoved} valueLabelDisplay="auto"/><br/>
            <Switch checked={this.state.logarithmic} onChange={this.handleChange} name="logarithmic" />Log scale<br/>
            <br/><br/>
            <SpotFindingPlot/>
          </Grid>
        </Grid>
        </div>;
  }
}

export default Preview;
